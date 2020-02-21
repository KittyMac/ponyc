use "collections"
use "files"
use "time"

type ProcessRunnerFinished is {(I32)}

actor ProcessRunner
  """
  A less weighty version of ProcessMonitor
  """
  var _stdin: _Pipe = _Pipe.none()
  var _stdout: _Pipe = _Pipe.none()
  var _stderr: _Pipe = _Pipe.none()
  var _child: _Process = _ProcessNone

  let _max_size: USize = 4096
  var _read_buf: Array[U8] iso = recover Array[U8] .> undefined(_max_size) end
  var _read_len: USize = 0
  var _expect: USize = 0

  var _pending: Array[ByteSeq] = Array[ByteSeq](128)
  var _pending_write_offset:USize = 0
  var _done_writing: Bool = false

  var _closed: Bool = false
  
  let _finishedCallback:ProcessRunnerFinished val
  
  fun _tag():USize => 9002
  fun _priority():USize => 100
  fun _batch():USize => 5_000_000

  new create(
    programPath: String val,
    args: Array[String] val,
    vars: Array[String] val,
    finishedCallback:ProcessRunnerFinished val)?
  =>
    _finishedCallback = finishedCallback
    _stdin = _Pipe.outgoing()?
    _stdout = _Pipe.incoming()?
    _stderr = _Pipe.incoming()?

    _child = _ProcessPosix.create(programPath, args, vars, _stdin, _stdout, _stderr)?

    _stdin.begin(this)
    _stdout.begin(this)
    _stderr.begin(this)

  be print(data: ByteSeq) =>
    """
    Print some bytes and append a newline.
    """
    if not _done_writing then
      _write_final(data)
      _write_final("\n")
    end

  be write(data: ByteSeq) =>
    """
    Write to STDIN of the child process.
    """
    if not _done_writing then
      _write_final(data)
    end

  be printv(data: ByteSeqIter) =>
    """
    Print an iterable collection of ByteSeqs.
    """
    for bytes in data.values() do
      _write_final(bytes)
      _write_final("\n")
    end

  be writev(data: ByteSeqIter) =>
    """
    Write an iterable collection of ByteSeqs.
    """
    for bytes in data.values() do
      _write_final(bytes)
    end

  be done_writing() =>
    """
    Set the _done_writing flag to true. If _pending is empty we can close the
    _stdin pipe.
    """
    _done_writing = true
    if _pending.size() == 0 then
      _stdin.close_near()
    end

  be dispose() =>
    """
    Terminate child and close down everything.
    """
    _child.kill()
    _close()

  be _event_notify(event: AsioEventID, flags: U32, arg: U32) =>
    """
    Handle the incoming Asio event from one of the pipes.
    """
    match event
    | _stdin.event =>
      if AsioEvent.writeable(flags) then
        _pending_writes()
      elseif AsioEvent.disposable(flags) then
        _stdin.dispose()
      end
    | _stdout.event =>
      if AsioEvent.readable(flags) then
        _pending_reads(_stdout)
      elseif AsioEvent.disposable(flags) then
        _stdout.dispose()
      end
    | _stderr.event =>
      if AsioEvent.readable(flags) then
        _pending_reads(_stderr)
      elseif AsioEvent.disposable(flags) then
        _stderr.dispose()
      end
    end
    _try_shutdown()

  fun ref _close() =>
    """
    Close all pipes and wait for the child process to exit.
    """
    if not _closed then
      _closed = true
      _stdin.close()
      _stdout.close()
      _stderr.close()
      let exit_code = _child.wait()
      
      _finishedCallback(exit_code)
      
    end

  fun ref _try_shutdown() =>
    """
    If neither stdout nor stderr are open we close down and exit.
    """
    if _stdin.is_closed() and
      _stdout.is_closed() and
      _stderr.is_closed()
    then
       _close()
    end

  fun ref _pending_reads(pipe: _Pipe) =>
    """
    Read from stdout or stderr while data is available. If we read 4 kb of
    data, send ourself a resume message and stop reading, to avoid starving
    other actors.
    It's safe to use the same buffer for stdout and stderr because of
    causal messaging. Events get processed one _after_ another.
    """
    if pipe.is_closed() then return end
    var sum: USize = 0
    while true do
      (_read_buf, let len, let errno) =
        pipe.read(_read_buf = recover Array[U8] end, _read_len)

      let next = _read_buf.space()
      match len
      | -1 =>
        if (errno == _EAGAIN()) then
          return // nothing to read right now, try again later
        end
        pipe.close()
        return
      | 0  =>
        pipe.close()
        return
      end

      _read_len = _read_len + len.usize()

      let data = _read_buf = recover Array[U8] .> undefined(next) end
      data.truncate(_read_len)

      match pipe.near_fd
      | _stdout.near_fd =>
        if _read_len >= _expect then
          None
          //_notifier.stdout(this, consume data)
        end
      | _stderr.near_fd =>
        None
        //_notifier.stderr(this, consume data)
      end

      _read_len = 0
      _read_buf_size()

      sum = sum + len.usize()
      if sum > (1 << 12) then
        // If we've read 4 kb, yield and read again later.
        _read_again(pipe.near_fd)
        return
      end
    end

  fun ref _read_buf_size() =>
    if _expect > 0 then
      _read_buf.undefined(_expect)
    else
      _read_buf.undefined(_max_size)
    end

  be _read_again(near_fd: U32) =>
    """
    Resume reading on file descriptor.
    """
    match near_fd
    | _stdout.near_fd => _pending_reads(_stdout)
    | _stderr.near_fd => _pending_reads(_stderr)
    end

  fun ref _write_final(data: ByteSeq) =>
    _pending.push(data)
    _pending_writes()

  fun ref _pending_writes() =>
    try
      while (not _closed) and not _stdin.is_closed() and (_pending.size() > 0) do
        let data = _pending(0)?
        
        (let len, let errno) = _stdin.write(data, _pending_write_offset)
        
        if len == -1 then // OS signals write error
          if errno == _EAGAIN() then
            return
          else
            _stdin.close_near()
            return
          end
        elseif (len.usize() + _pending_write_offset) < data.size() then
          _pending_write_offset = _pending_write_offset + len.usize()
        else
          // This pending chunk has been fully sent.
          _pending.shift()?
          _pending_write_offset = 0
          
          if (_pending.size() == 0) then
            if _done_writing then
              _stdin.close_near()
            end
          end
        end
      end
    end
    
