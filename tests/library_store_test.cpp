#include "library/store.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <set>

using namespace playden;

static void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", description);
    std::exit(1);
  }
}

int main() {
  constexpr const char* kUuid = "1b4e28ba-2fa1-11d2-883f-0016d3cca427";

  Check(!GameId{}.valid(), "a default GameId is invalid");
  Check(!GameId::Steam(0).valid() && GameId::Steam(0) == GameId{}, "Steam app 0 is invalid");
  Check(GameId::Steam(440).valid() && GameId::Steam(440).value == "440", "Steam value is canonical decimal");
  Check(GameId::Steam(440).steam_app() == 440u, "steam_app returns the app ID");
  Check(!GameId::Local(kUuid).steam_app(), "a Local game has no Steam app");
  Check(GameId::Local(kUuid).valid(), "a lower-case UUID is a valid Local value");
  Check(!GameId::Local("1B4E28BA-2FA1-11D2-883F-0016D3CCA427").valid(), "an upper-case UUID is not a valid Local value");
  Check(!(GameId{Store::kSteam, "0440"}).valid(), "a Steam value with a leading zero is invalid");
  Check(!(GameId{Store::kGog, "1"}).valid(), "GOG is not implemented yet");

  constexpr const char* kEpicHex = "d8b1f3a09c2e4d7fa6b5018c3e9d2f47";
  Check(GameId::Epic(kEpicHex).valid() && GameId::Epic("Fortnite").valid(), "Epic hex and text names are valid");
  Check(GameId::Epic("").value.empty() && GameId::Epic("") == GameId{}, "an empty Epic name is invalid");
  Check(!GameId::Epic("Bad Name").valid() && !GameId::Epic("Bad-Name").valid(), "Epic names are letters and digits only");
  Check(!GameId::Epic("ABCDEF0123456789ABCDEF0123456789").valid(), "a 32-digit upper-case hex name is not accepted");
  Check(!GameId::Epic(std::string(26, 'a')).valid(), "a text name over 25 characters is not accepted");
  Check(!GameId::Epic(kEpicHex).steam_app(), "an Epic game has no Steam app");

  const auto max = GameId::Steam(std::numeric_limits<std::uint32_t>::max());
  Check(ToKey(max) == "4294967295" && ParseKey("4294967295") == max, "UINT32_MAX round trips");
  Check(ToKey(GameId::Steam(440)) == "440", "Steam key is the bare app ID");
  Check(ParseKey("440") == GameId::Steam(440), "bare digits parse as Steam");
  Check(ToKey(GameId::Local(kUuid)) == std::string("local-") + kUuid, "Local key has the local- prefix");
  Check(ParseKey(std::string("local-") + kUuid) == GameId::Local(kUuid), "Local key round trips");

  for (const char* name : {"Fortnite", "Salt", "a", "Z9", "ABCDEFGHIJKLMNOPQRSTUVWXY", kEpicHex,
                           "00000000000000000000000000000000", "ffffffffffffffffffffffffffffffff"}) {
    const auto epic = GameId::Epic(name);
    Check(epic.valid() && ParseKey(ToKey(epic)) == epic, "an Epic key round trips");
    Check(ParseInstallId(ToInstallId(epic)) == epic, "an Epic install ID round trips");
    Check(ToKey(epic).rfind("epic-", 0) == 0 && ToKey(epic) == ToInstallId(epic), "Epic key is the install ID");
  }
  Check(ToKey(GameId::Epic("Fortnite")) != ToKey(GameId::Epic("fortnite")), "Epic names keep their case in the key");
  Check(ToKey(GameId::Epic(kEpicHex)).size() == 5 + 27, "a hex name is 27 characters after the prefix");
  Check(ToKey(GameId::Epic("Fortnite")) == "epic-tizxxe5donf2gk", "Epic text name key is stable");
  Check(ParseKey("epic-tizxxe5donf2gk") == GameId::Epic("Fortnite"), "a stored Epic key still parses");
  for (const char* rejected : {"epic-", "epic-h", "epic-t", "epic-x", "epic-TIZXXE5DONF2GK", "epic-tizxxe5donf2gl",
                               "epic-tizxxe5donf2gk=", "epic-hmfrgg", "epic-t" "mfrgg" "ggg", "Epic-tijxg4zlooj3gk"}) {
    if (ParseKey(rejected)) {
      std::fprintf(stderr, "FAIL: ParseKey accepted \"%s\"\n", rejected);
      return 1;
    }
  }
  Check(GameId::Steam(4294967295u) < GameId::Epic("Fortnite") && GameId::Epic("Fortnite") < GameId::Local(kUuid),
        "Epic sorts between Steam and Local");

  for (const char* rejected : {"", "0", "0440", "4294967296", "99999999999999999999", "+1", "-1", " 440", "440 ",
                               "440x", "local-", "local-1B4E28BA-2FA1-11D2-883F-0016D3CCA427",
                               "local-1b4e28ba2fa111d2883f0016d3cca427", "local-1b4e28ba-2fa1-11d2-883f-0016d3cca42",
                               "local-1b4e28ba-2fa1-11d2-883f-0016d3cca427-", "gog-1", "steam-440", "LOCAL-x"}) {
    if (ParseKey(rejected)) {
      std::fprintf(stderr, "FAIL: ParseKey accepted \"%s\"\n", rejected);
      return 1;
    }
  }

  Check(GameId::Steam(9) < GameId::Steam(10), "Steam orders numerically");
  Check(!(GameId::Steam(10) < GameId::Steam(9)), "Steam numeric order is strict");
  Check(GameId::Steam(4294967295u) < GameId::Local(kUuid), "Steam sorts before Local");
  const std::set<GameId> ordered{GameId::Steam(100), GameId::Steam(9), GameId::Steam(20)};
  Check(*ordered.begin() == GameId::Steam(9) && *ordered.rbegin() == GameId::Steam(100), "set order is numeric");
  Check(GameId::Steam(440) != GameId::Steam(441) && !(GameId::Steam(440) != GameId::Steam(440)), "inequality");
  Check(GameId{Store::kSteam, "1"} != GameId{Store::kLocal, "1"}, "the store is part of the identity");

  const std::hash<GameId> hash;
  Check(hash(GameId::Steam(440)) == hash(*ParseKey("440")), "equal Steam ids hash equally");
  Check(hash(GameId::Local(kUuid)) == hash(*ParseKey(std::string("local-") + kUuid)), "equal Local ids hash equally");

  // Steam derived IDs keep today's exact text.
  const auto steam = GameId::Steam(440);
  Check(ToInstallId(steam) == "steam-440", "Steam install ID");
  Check(ParseInstallId("steam-440") == steam, "Steam install ID parses");
  Check(GeneratedRecipeId(steam) == "steam-440-auto", "Steam generated recipe ID");
  Check(ProfileId(steam) == "playden-steam-440", "Steam profile ID");
  Check(!ParseInstallId("steam-0440") && !ParseInstallId("steam-") && !ParseInstallId("440") && !ParseInstallId("gog-1"),
        "malformed install IDs are rejected");

  const auto local = GameId::Local(kUuid);
  Check(ToInstallId(local) == std::string("local-") + kUuid, "Local install ID");
  Check(ParseInstallId(ToInstallId(local)) == local, "Local install ID parses");
  Check(GeneratedRecipeId(local) == std::string("local-") + kUuid + "-auto", "Local generated recipe ID");
  Check(ProfileId(local) == std::string("playden-local-") + kUuid, "Local profile ID");

  for (const auto& id : {max, local, GameId::Epic(kEpicHex), GameId::Epic("ABCDEFGHIJKLMNOPQRSTUVWXY")}) {
    for (const auto& derived : {ToKey(id), ToInstallId(id), GeneratedRecipeId(id), ProfileId(id)}) {
      bool allowed = derived.size() <= 64;
      for (const char character : derived)
        allowed = allowed && ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
                              character == '-');
      if (!allowed) {
        std::fprintf(stderr, "FAIL: derived ID \"%s\" is not [a-z0-9-] within 64 bytes\n", derived.c_str());
        return 1;
      }
    }
    Check(ToKey(id).size() <= 48, "keys fit in 48 bytes");
  }

  Check(Traits(Store::kSteam).key == "steam" && Traits(Store::kSteam).name == "Steam" &&
            Traits(Store::kSteam).needs_account && Traits(Store::kSteam).cloud_saves && Traits(Store::kSteam).downloads,
        "Steam traits");
  Check(Traits(Store::kLocal).key == "local" && Traits(Store::kLocal).name == "Imported" &&
            !Traits(Store::kLocal).needs_account && !Traits(Store::kLocal).cloud_saves && !Traits(Store::kLocal).downloads,
        "Local traits");
  Check(Traits(Store::kGog).key == "gog" && Traits(Store::kGog).name == "GOG" && Traits(Store::kGog).needs_account &&
            !Traits(Store::kGog).cloud_saves && Traits(Store::kGog).downloads,
        "GOG traits");
  Check(Traits(Store::kEpic).key == "epic" && Traits(Store::kEpic).name == "Epic" && Traits(Store::kEpic).needs_account &&
            !Traits(Store::kEpic).cloud_saves && Traits(Store::kEpic).downloads,
        "Epic traits");
  Check(static_cast<int>(Store::kSteam) == 1 && static_cast<int>(Store::kGog) == 2 &&
            static_cast<int>(Store::kEpic) == 3 && static_cast<int>(Store::kLocal) == 4,
        "persisted store values");

  std::puts("library_store: ok");
  return 0;
}
