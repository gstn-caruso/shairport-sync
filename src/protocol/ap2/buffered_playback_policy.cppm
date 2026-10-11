export module receiver.protocol.ap2.buffered_playback;

export struct BufferedPlayAction {
  bool started = false, stopped = false, resetPlayer = false, needFreshBlock = false;
};
export class BufferedPlaybackPolicy {
public:
  explicit BufferedPlaybackPolicy(double desiredBufferSeconds) : desiredBufferSeconds_(desiredBufferSeconds) {}
  BufferedPlayAction onPlayState(bool enabled);
private:
  double desiredBufferSeconds_;
  bool playing_ = false;
};
