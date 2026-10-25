if(NOT NRI_SOURCE_DIR)
    message(FATAL_ERROR "NRI_SOURCE_DIR is required")
endif()

# Destroy<T> read the device's allocation callbacks through the object after
# running its destructor. Copy them first; upstream NRI made the same change.
set(_path "${NRI_SOURCE_DIR}/Source/Shared/DeviceBase.h")
file(READ "${_path}" _source)
set(_old [=[
    if (object) {
        object->~T();

        const auto& allocationCallbacks = ((DeviceBase&)(object->GetDevice())).GetAllocationCallbacks();
        allocationCallbacks.Free(allocationCallbacks.userArg, object);
    }
]=])
set(_new [=[
    if (object) {
        const AllocationCallbacks allocationCallbacks = ((DeviceBase&)(object->GetDevice())).GetAllocationCallbacks(); // OOT3D: read before destroying
        object->~T();

        allocationCallbacks.Free(allocationCallbacks.userArg, object);
    }
]=])
string(FIND "${_source}" "${_new}" _already_patched)
if(NOT _already_patched EQUAL -1)
    return()
endif()
string(FIND "${_source}" "${_old}" _patch_site)
if(_patch_site EQUAL -1)
    message(FATAL_ERROR "NRI Source/Shared/DeviceBase.h changed; update the OOT3D Destroy lifetime patch")
endif()
string(REPLACE "${_old}" "${_new}" _source "${_source}")
file(WRITE "${_path}" "${_source}")
