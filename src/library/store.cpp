#include "library/store.h"

#include <charconv>
#include <system_error>
#include <utility>

namespace playden {
namespace {

constexpr StoreTraits kSteamTraits{Store::kSteam, "steam", "Steam", true, true, true};
constexpr StoreTraits kGogTraits{Store::kGog, "gog", "GOG", true, false, true};
constexpr StoreTraits kEpicTraits{Store::kEpic, "epic", "Epic", true, false, true};
constexpr StoreTraits kLocalTraits{Store::kLocal, "local", "Imported", false, false, false};

constexpr std::string_view kLocalPrefix = "local-";
constexpr std::string_view kEpicPrefix = "epic-";

// Epic app names are case-sensitive, but keys are [a-z0-9-]. A 32-digit lower-case hex name (a catalog ID)
// becomes 'h' plus its 16 bytes in lower-case base32 (26 characters); any other name of 1..25 ASCII letters
// and digits becomes 't' plus its bytes in the same base32. The first letter says which, so decoding is exact.
constexpr std::size_t kEpicTextMax = 25;
constexpr std::string_view kBase32 = "abcdefghijklmnopqrstuvwxyz234567";

bool IsLowerHex(char character);

bool IsEpicHex(std::string_view text) {
  if (text.size() != 32) return false;
  for (const char character : text)
    if (!IsLowerHex(character)) return false;
  return true;
}

bool IsEpicText(std::string_view text) {
  if (text.empty() || text.size() > kEpicTextMax) return false;
  for (const char character : text) {
    const bool alnum = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                       (character >= '0' && character <= '9');
    if (!alnum) return false;
  }
  return true;
}

bool IsEpicValue(std::string_view text) { return IsEpicHex(text) || IsEpicText(text); }

std::string Base32Encode(std::string_view bytes) {
  std::string out;
  std::uint32_t buffer = 0;
  int bits = 0;
  for (const char byte : bytes) {
    buffer = (buffer << 8) | static_cast<unsigned char>(byte);
    bits += 8;
    while (bits >= 5) {
      out.push_back(kBase32[(buffer >> (bits - 5)) & 31]);
      bits -= 5;
    }
    buffer &= (1u << bits) - 1;
  }
  if (bits > 0) out.push_back(kBase32[(buffer << (5 - bits)) & 31]);
  return out;
}

// Canonical only: characters from the alphabet and zero padding bits, so each byte string has one encoding.
std::optional<std::string> Base32Decode(std::string_view text) {
  std::string out;
  std::uint32_t buffer = 0;
  int bits = 0;
  for (const char character : text) {
    const auto position = kBase32.find(character);
    if (position == std::string_view::npos) return std::nullopt;
    buffer = (buffer << 5) | static_cast<std::uint32_t>(position);
    bits += 5;
    if (bits >= 8) {
      out.push_back(static_cast<char>((buffer >> (bits - 8)) & 0xff));
      bits -= 8;
    }
    buffer &= (1u << bits) - 1;
  }
  if (bits >= 5 || buffer != 0) return std::nullopt;
  return out;
}

std::string HexToBytes(std::string_view hex) {
  std::string out;
  for (std::size_t index = 0; index < hex.size(); index += 2) {
    const auto nibble = [](char character) { return character <= '9' ? character - '0' : character - 'a' + 10; };
    out.push_back(static_cast<char>((nibble(hex[index]) << 4) | nibble(hex[index + 1])));
  }
  return out;
}

std::string BytesToHex(std::string_view bytes) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string out;
  for (const char byte : bytes) {
    out.push_back(digits[(static_cast<unsigned char>(byte) >> 4) & 15]);
    out.push_back(digits[static_cast<unsigned char>(byte) & 15]);
  }
  return out;
}

std::string EncodeEpic(std::string_view value) {
  if (IsEpicHex(value)) return "h" + Base32Encode(HexToBytes(value));
  return "t" + Base32Encode(value);
}

std::optional<std::string> DecodeEpic(std::string_view encoded) {
  if (encoded.size() < 2) return std::nullopt;
  const auto bytes = Base32Decode(encoded.substr(1));
  if (!bytes) return std::nullopt;
  std::string value;
  if (encoded.front() == 'h') {
    if (bytes->size() != 16) return std::nullopt;
    value = BytesToHex(*bytes);
  } else if (encoded.front() == 't') {
    value = *bytes;
    if (IsEpicHex(value)) return std::nullopt;  // a hex name has only the 'h' form
  } else {
    return std::nullopt;
  }
  if (!IsEpicValue(value) || EncodeEpic(value) != encoded) return std::nullopt;
  return value;
}

std::optional<std::uint32_t> ParseSteamValue(std::string_view text) {
  if (text.empty() || text.front() == '0') return std::nullopt;
  std::uint32_t value = 0;
  const auto* end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value);
  if (result.ec != std::errc{} || result.ptr != end || value == 0) return std::nullopt;
  return value;
}

bool IsLowerHex(char character) {
  return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
}

// 8-4-4-4-12 lower-case hex.
bool IsLocalValue(std::string_view text) {
  if (text.size() != 36) return false;
  for (std::size_t index = 0; index < text.size(); ++index) {
    const bool dash = index == 8 || index == 13 || index == 18 || index == 23;
    if (dash ? text[index] != '-' : !IsLowerHex(text[index])) return false;
  }
  return true;
}

}  // namespace

const StoreTraits& Traits(Store store) {
  switch (store) {
    case Store::kSteam: return kSteamTraits;
    case Store::kGog: return kGogTraits;
    case Store::kEpic: return kEpicTraits;
    case Store::kLocal: return kLocalTraits;
  }
  return kSteamTraits;
}

std::optional<Store> ParseStoreKey(std::string_view key) {
  if (key == kLocalTraits.key) return Store::kLocal;
  return std::nullopt;
}

bool GameId::valid() const {
  switch (store) {
    case Store::kSteam: return ParseSteamValue(value).has_value();
    case Store::kLocal: return IsLocalValue(value);
    case Store::kEpic: return IsEpicValue(value);
    case Store::kGog: return false;  // not implemented yet
  }
  return false;
}

std::optional<std::uint32_t> GameId::steam_app() const {
  if (store != Store::kSteam) return std::nullopt;
  return ParseSteamValue(value);
}

GameId GameId::Steam(std::uint32_t app) {
  if (app == 0) return {};
  return {Store::kSteam, std::to_string(app)};
}

GameId GameId::Local(std::string uuid) { return {Store::kLocal, std::move(uuid)}; }

GameId GameId::Epic(std::string app_name) {
  if (!IsEpicValue(app_name)) return {};
  return {Store::kEpic, std::move(app_name)};
}

bool operator==(const GameId& left, const GameId& right) {
  return left.store == right.store && left.value == right.value;
}

bool operator!=(const GameId& left, const GameId& right) { return !(left == right); }

bool operator<(const GameId& left, const GameId& right) {
  if (left.store != right.store) return left.store < right.store;
  // Canonical decimals: the shorter number is the smaller one.
  if (left.store == Store::kSteam && left.value.size() != right.value.size())
    return left.value.size() < right.value.size();
  return left.value < right.value;
}

std::string ToKey(const GameId& id) {
  if (id.store == Store::kLocal) return std::string(kLocalPrefix) + id.value;
  if (id.store == Store::kEpic) return std::string(kEpicPrefix) + EncodeEpic(id.value);
  return id.value;
}

std::optional<GameId> ParseKey(std::string_view key) {
  if (key.substr(0, kLocalPrefix.size()) == kLocalPrefix) {
    const auto value = key.substr(kLocalPrefix.size());
    if (!IsLocalValue(value)) return std::nullopt;
    return GameId::Local(std::string(value));
  }
  if (key.substr(0, kEpicPrefix.size()) == kEpicPrefix) {
    const auto value = DecodeEpic(key.substr(kEpicPrefix.size()));
    if (!value) return std::nullopt;
    return GameId::Epic(*value);
  }
  if (const auto app = ParseSteamValue(key)) return GameId::Steam(*app);
  return std::nullopt;
}

std::string ToInstallId(const GameId& id) {
  if (id.store == Store::kSteam) return "steam-" + id.value;
  return ToKey(id);
}

std::optional<GameId> ParseInstallId(std::string_view install_id) {
  constexpr std::string_view kSteamPrefix = "steam-";
  if (install_id.substr(0, kSteamPrefix.size()) == kSteamPrefix) {
    const auto app = ParseSteamValue(install_id.substr(kSteamPrefix.size()));
    if (!app) return std::nullopt;
    return GameId::Steam(*app);
  }
  if (install_id.substr(0, kLocalPrefix.size()) == kLocalPrefix) return ParseKey(install_id);
  if (install_id.substr(0, kEpicPrefix.size()) == kEpicPrefix) return ParseKey(install_id);
  return std::nullopt;
}

std::string GeneratedRecipeId(const GameId& id) { return ToInstallId(id) + "-auto"; }

std::string ProfileId(const GameId& id) { return "playden-" + ToInstallId(id); }

}  // namespace playden
