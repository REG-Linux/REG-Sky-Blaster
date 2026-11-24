// LAN co-op over enet (UDP). The host runs the game and sends a snapshot
// of it each frame, with the effects (particles, sounds) its ticks made;
// the guest sends its input each tick, moves its own ship at once and is
// corrected by the snapshots. A beacon on BEACON_PORT tells lobbies (the
// REG-Linux frontend's) where a host is.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../game/game.h"

typedef struct _ENetHost ENetHost;
typedef struct _ENetPeer ENetPeer;

namespace net {

constexpr uint16_t BEACON_PORT = 7070;
constexpr uint16_t GAME_PORT = 7071;

class Session {
 public:
  ~Session();
  // Host: listens and announces itself. Guest: connects (retrying until the
  // host answers). Both set g.netRole and g.netStatus.
  bool host(GameData& g, const std::string& name);
  bool join(GameData& g, const std::string& address, const std::string& name);

  // Once per frame, before the ticks: network events and messages.
  void service(GameData& g);
  // Host: the effects its ticks record go here (set as fxRecorder).
  std::vector<FxEvent>* recorder() { return &fx_; }
  // Guest, each tick: sends its input (one-shots consumed) and logs it.
  void guestTick(GameData& g, InputState& in);
  // Once per frame, after the ticks: the host sends what changed.
  void sync(GameData& g);
  // The field this screen would have alone (sent with the hello).
  void setOwnField(float w, float h) { ownW_ = w, ownH_ = h; }
  // Host, with a guest: the field a run shares, the narrower and shorter of
  // the two screens' own (no tiny picture or one across a seam on either).
  // False (w, h untouched) without one.
  bool commonField(float& w, float& h) const;
  void shutdown();

 private:
  struct LoggedInput {
    uint32_t tick;
    InputState in;
  };
  void onConnect(GameData& g, ENetPeer* peer);
  void onDisconnect(GameData& g, ENetPeer* peer);
  void onReceive(GameData& g, ENetPeer* peer, const uint8_t* data, size_t len);
  void sendHello();
  void sendStart(GameData& g);
  void sendSnapshot(GameData& g);
  void applySnapshot(GameData& g, const uint8_t* data, size_t len);
  void beginGuestRun(GameData& g, const uint8_t* data, size_t len);
  void lose(GameData& g, NetStatus why);
  void beacon(const GameData& g);
  void connectToHost();
  void send(const std::vector<uint8_t>& msg, bool reliable);

  ENetHost* enet_ = nullptr;
  ENetPeer* peer_ = nullptr;  // the other player, once connected
  bool helloed_ = false;      // its hello came and matched
  bool isHost_ = false;
  std::string name_, address_;
  int beaconFd_ = -1;
  uint64_t nextBeaconMs_ = 0, nextConnectMs_ = 0;
  // host
  std::vector<FxEvent> fx_;
  uint32_t lastGuestTick_ = 0;
  int sentRun_ = 0;
  GameState sentState_ = GameState::Menu;
  // guest
  uint32_t tick_ = 0;
  std::vector<LoggedInput> log_;
  AircraftId sentAircraft_ = AircraftId::Count;
  WeaponId sentWeapon_ = WeaponId::Count;
  float fieldW_ = 0, fieldH_ = 0;  // the host's field, during a run
  float ownW_ = 0, ownH_ = 0;      // ours alone
  float peerW_ = 0, peerH_ = 0;    // the guest's own (host)
  float sentW_ = 0, sentH_ = 0;    // the field our last hello carried

 public:
  // The guest draws the host's field (letterboxed): 0 when it is free.
  float fieldW() const { return fieldW_; }
  float fieldH() const { return fieldH_; }
};

}  // namespace net
