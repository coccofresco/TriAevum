#pragma once
#include "mm3d_hook_sites.h"
#include "oot3d_dev_free_camera.h"
#include "triaevum_guest_transaction.h"
#include <array>
#include <cmath>

namespace Oot3dNativeGame {
// Product photo-camera ABI, independent of developer warps/catalogues. Only the
// active main gameplay camera may own input; menus, fades and scripted cameras
// retain their native update. EUR v1.1 facts qualified by DEVWARP receipts and
// MAP_7 G1.08. This does not replace MM's native C-stick camera while inactive.
class Mm3dPhotoCamera {
  mutable uint32_t mLastPlay=0,mLastFrame=0;
  mutable int16_t mLastScene=-1;
  mutable bool mRebaseAfterReset=false, mHasNative=false;
  struct Pose {
    std::array<float,3> At{}, Eye{}, Up{}, EyeNext{}, ViewEye{}, ViewAt{}, ViewUp{};
    std::array<int16_t,3> InputDirection{}, CameraDirection{};
  };
  mutable Pose mNative;
  mutable std::array<float,3> mPlayerOrigin{};
  struct Receipt {
    uint64_t Calls=0, ActiveCalls=0, InactiveCalls=0, Rejected=0, Applied=0;
    uint64_t Restored=0, Rebased=0, Resets=0, WriteFailures=0;
    float MaxEyeDistance=0, MaxPlayerDistance=0;
    std::array<float,3> LastEye{}, LastNativeEye{};
  };
  mutable Receipt mReceipt;
  template<class G> static bool ReadPose(G& g,uint32_t p,uint32_t c,Pose& pose) {
    return ReadGuestValue(g,c+0x50,pose.At)&&ReadGuestValue(g,c+0x5C,pose.Eye)&&
      ReadGuestValue(g,c+0x68,pose.Up)&&ReadGuestValue(g,c+0x74,pose.EyeNext)&&
      ReadGuestValue(g,p+0x220,pose.ViewEye)&&ReadGuestValue(g,p+0x22C,pose.ViewAt)&&
      ReadGuestValue(g,p+0x238,pose.ViewUp)&&ReadGuestValue(g,c+0x148,pose.InputDirection)&&
      ReadGuestValue(g,c+0x14E,pose.CameraDirection);
  }
  template<class G> static bool AddPose(GuestWriteTransaction<G>& tx,uint32_t p,uint32_t c,const Pose& pose) {
    return tx.Add(c+0x50,pose.At)&&tx.Add(c+0x5C,pose.Eye)&&tx.Add(c+0x68,pose.Up)&&
      tx.Add(c+0x74,pose.EyeNext)&&tx.Add(p+0x220,pose.ViewEye)&&tx.Add(p+0x22C,pose.ViewAt)&&
      tx.Add(p+0x238,pose.ViewUp)&&tx.Add(c+0x148,pose.InputDirection)&&tx.Add(c+0x14E,pose.CameraDirection);
  }
  static float Distance(const std::array<float,3>& a,const std::array<float,3>& b) {
    float d=0;for(unsigned i=0;i<3;++i)d+=(a[i]-b[i])*(a[i]-b[i]);return std::sqrt(d);
  }
  static bool Pointer(uint32_t p,uint32_t size) {
    return p>=0x08000000 && !(p&3U) && p<0x40000000 && size<=0x40000000-p;
  }
  static bool Finite(const std::array<float,3>& v) {
    for(float x:v)if(!std::isfinite(x)||std::abs(x)>1e7F)return false;
    return true;
  }
 public:
  template<class G> uint32_t Play(G& g) const {
    uint32_t view=0,owner=0,right=0,bottom=0; uint8_t type=0;
    if(!ReadGuestValue(g,0x656B5C,view)||view<0x1F8||!Pointer(view-0x1F8,0x11030))return 0;
    const uint32_t p=view-0x1F8;
    if(!ReadGuestValue(g,p+0x13D,type)||type!=4||!ReadGuestValue(g,p+0x4A8,owner)||owner!=p||
       !ReadGuestValue(g,view+8,right)||right!=400||!ReadGuestValue(g,view+16,bottom)||bottom!=240)return 0;
    return p;
  }
  template<class G> bool OwnsInput(G& g) const {
    const auto read=[&](uint32_t a,uint32_t& w){return ReadGuestValue(g,a,w);};
    if(!Mm3dHookSiteMatches(kMm3dCameraUpdateSite,read)||!Mm3dHookSiteMatches(kMm3dPlayUpdateSite,read)||
       !Mm3dHookSiteMatches(kMm3dTransitionSite,read))return false;
    const uint32_t p=Play(g); if(!p)return false;
    uint8_t state=0,trigger=0,mode=0,form=0,category=0;
    uint16_t pause=0,index=0,overrideEntrance=0,id=1,status=0,setting=0,actorId=1;
    uint32_t player=0,slot=0; int32_t gameMode=0,cutscene=0;
    std::array<float,3> position{};
    return ReadGuestValue(g,p+0x13C,state)&&state==2&&
      ReadGuestValue(g,0x7751D8+0x1361C,gameMode)&&gameMode==0&&
      ReadGuestValue(g,0x7751D8+0x1C,cutscene)&&cutscene>=0&&cutscene<0xFFF0&&
      ReadGuestValue(g,0x7751D8+0x26,form)&&form<=4&&
      ReadGuestValue(g,p+0xAAC,pause)&&!pause&&ReadGuestValue(g,p+0xA7A,index)&&!index&&
      ReadGuestValue(g,p+0xC529,trigger)&&!trigger&&ReadGuestValue(g,p+0xC85A,mode)&&!mode&&
      ReadGuestValue(g,p+0xCCAA,overrideEntrance)&&!overrideEntrance&&
      ReadGuestValue(g,p+0x20DC,player)&&Pointer(player,0x1F8)&&
      ReadGuestValue(g,player,actorId)&&actorId==0&&ReadGuestValue(g,player+2,category)&&category==2&&
      ReadGuestValue(g,player+0x24,position)&&Finite(position)&&
      ReadGuestValue(g,p+0xA68,slot)&&slot==p+0x408&&
      ReadGuestValue(g,slot+0x178,id)&&id==0&&ReadGuestValue(g,slot+0x154,status)&&status==7&&
      ReadGuestValue(g,slot+0x156,setting)&&setting!=9&&setting!=10;
  }
  template<class G> bool Apply(G& g,DevFreeCamera& free,uint32_t out,uint32_t camera) const {
    ++mReceipt.Calls;
    const bool active=free.Active();
    if(active)++mReceipt.ActiveCalls;else ++mReceipt.InactiveCalls;
    if(!active&&!mHasNative)return false; // optional-off performs no guest reads or writes
    if(!OwnsInput(g)){++mReceipt.Rejected;mHasNative=false;mRebaseAfterReset=true;return false;}
    const uint32_t p=Play(g);
    if(camera!=p+0x408||!Pointer(out,6)){++mReceipt.Rejected;return false;}
    uint32_t frame=0; int16_t scene=-1;
    if(!ReadGuestValue(g,p+0x138,frame)||!ReadGuestValue(g,p+0x148,scene))return false;
    if(mLastPlay&&(p!=mLastPlay||scene!=mLastScene||frame<mLastFrame)) {
      mHasNative=false;mRebaseAfterReset=true;
    }
    // Never replay a pre-restore/previous-scene pose into the new camera.
    if(mRebaseAfterReset){free.ResetToGame();mRebaseAfterReset=false;++mReceipt.Rebased;}
    if(!active) {
      if(mHasNative) {
        GuestWriteTransaction<G> tx(g);
        if(!AddPose(tx,p,camera,mNative)||!tx.Commit()){++mReceipt.WriteFailures;return false;}
        ++mReceipt.Restored;mHasNative=false;
      }
      return false; // restored native inputs; execute the original Camera_Update
    }
    Pose native{};
    uint32_t player=0;std::array<float,3> playerPosition{};
    if(!ReadPose(g,p,camera,native)||!Finite(native.Eye)||!Finite(native.At)||
       !ReadGuestValue(g,p+0x20DC,player)||!ReadGuestValue(g,player+0x24,playerPosition)||!Finite(playerPosition))return false;
    // ResetToGame from F1/Esc must seed from the retained native view, not the
    // photo view written by our last callback. It does not move the player.
    const Pose& seed=mHasNative?mNative:native;
    const auto view=free.ResolveView(seed.Eye,seed.At,false);
    if(!view||!Finite(view->Eye)||!Finite(view->At))return false;
    GuestWriteTransaction<G> tx(g);
    const std::array<float,3> up{0,1,0};
    const std::array<int16_t,3> direction{view->Pitch,view->Yaw,0};
    if(!tx.Add(camera+0x50,view->At)||!tx.Add(camera+0x5C,view->Eye)||!tx.Add(camera+0x68,up)||
       !tx.Add(camera+0x74,view->Eye)||!tx.Add(p+0x220,view->Eye)||!tx.Add(p+0x22C,view->At)||
       !tx.Add(p+0x238,up)||!tx.Add(camera+0x148,direction)||!tx.Add(camera+0x14E,direction)||
       !tx.Add(out,direction)||!tx.Commit()){++mReceipt.WriteFailures;return false;}
    if(!mHasNative){mNative=native;mPlayerOrigin=playerPosition;mHasNative=true;}
    ++mReceipt.Applied;
    mReceipt.LastEye=view->Eye;mReceipt.LastNativeEye=mNative.Eye;
    mReceipt.MaxEyeDistance=std::max(mReceipt.MaxEyeDistance,Distance(view->Eye,mNative.Eye));
    mReceipt.MaxPlayerDistance=std::max(mReceipt.MaxPlayerDistance,Distance(playerPosition,mPlayerOrigin));
    free.MarkApplied(*view); mLastPlay=p; mLastScene=scene; mLastFrame=frame; return true;
  }
  const auto& Stats() const {return mReceipt;}
  void Reset() {
    mLastPlay=0;mLastFrame=0;mLastScene=-1;mHasNative=false;mRebaseAfterReset=true;++mReceipt.Resets;
  }
};
} // namespace Oot3dNativeGame
