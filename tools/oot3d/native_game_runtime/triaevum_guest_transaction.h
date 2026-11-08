#pragma once
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

namespace Oot3dNativeGame {
template<class Guest, class T> bool ReadGuestValue(Guest& guest,uint32_t address,T& value) {
  static_assert(std::endian::native==std::endian::little);
  return guest.Read(address,std::span<uint8_t>(reinterpret_cast<uint8_t*>(&value),sizeof(value)));
}
// Preflight every field before Commit. A partial failed write is included in
// rollback. Unrecoverable rollback stops execution instead of publishing a
// partially changed camera/transaction.
template<class Guest> class GuestWriteTransaction {
  struct Write { uint32_t Address; std::vector<uint8_t> Before,After; };
  Guest& mGuest;
  std::vector<Write> mWrites;
 public:
  explicit GuestWriteTransaction(Guest& guest):mGuest(guest){}
  template<class T> bool Add(uint32_t address,const T& value) {
    Write w{address,std::vector<uint8_t>(sizeof(T)),std::vector<uint8_t>(sizeof(T))};
    if(!mGuest.Read(address,w.Before))return false;
    std::memcpy(w.After.data(),&value,sizeof(T));
    mWrites.push_back(std::move(w)); return true;
  }
  bool Commit() {
    for(size_t i=0;i<mWrites.size();++i) {
      if(mGuest.Write(mWrites[i].Address,mWrites[i].After))continue;
      bool restored=true;
      for(size_t j=i+1;j!=0;--j)
        restored=mGuest.Write(mWrites[j-1].Address,mWrites[j-1].Before)&&restored;
      if(!restored)throw std::runtime_error("Guest transaction rollback failed; stop guest execution");
      return false;
    }
    return true;
  }
};
// Byte adapter shared by optional title services, deliberately no native calls.
template<class Bus> class GuestByteAccess {
  Bus& mBus;
 public:
  explicit GuestByteAccess(Bus& bus):mBus(bus){}
  bool Read(uint32_t a,std::span<uint8_t> bytes) {
    if(bytes.size()>UINT32_MAX-a)return false;
    for(size_t i=0;i<bytes.size();++i)if(!mBus.Read8(a+uint32_t(i),&bytes[i]))return false;
    return true;
  }
  bool Write(uint32_t a,std::span<const uint8_t> bytes) {
    if(bytes.size()>UINT32_MAX-a)return false;
    for(size_t i=0;i<bytes.size();++i)if(!mBus.Write8(a+uint32_t(i),bytes[i]))return false;
    return true;
  }
};
} // namespace Oot3dNativeGame
