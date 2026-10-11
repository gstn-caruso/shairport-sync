module receiver.protocol.ap2.buffered_playback;

BufferedPlayAction BufferedPlaybackPolicy::onPlayState(bool enabled) {
  BufferedPlayAction action;
  action.started = !playing_ && enabled;
  action.stopped = playing_ && !enabled;
  action.resetPlayer = action.stopped;
  action.needFreshBlock = action.started;
  playing_ = enabled;
  return action;
}
