// CTR host services: savestate capture and restore of the host service state.
#include "oot3d_native_a32_ctr_host_internal.h"

namespace Oot3dNativeGame {

nlohmann::json NativeA32CtrHostServices::CaptureState() const {
    std::unordered_map<const KernelObject*, uint32_t> objectIds;
    std::vector<std::shared_ptr<KernelObject>> objects;
    const auto registerObject =
        [&](const std::shared_ptr<KernelObject>& object) {
            if (!object) {
                return 0U;
            }
            const auto found = objectIds.find(object.get());
            if (found != objectIds.end()) {
                return found->second;
            }
            const uint32_t id = static_cast<uint32_t>(objects.size()) + 1U;
            objectIds.emplace(object.get(), id);
            objects.push_back(object);
            return id;
        };
    for (const auto& handle : mHandles) {
        registerObject(handle.Object);
    }
    for (const auto& waiter : mArbiterWaiters) {
        registerObject(waiter.Arbiter);
    }
    for (const auto& waiter : mSynchronizationWaiters) {
        for (const auto& object : waiter.Objects) {
            registerObject(object);
        }
    }
    for (const auto& object :
         {mAptLock, mAptNotificationEvent, mAptParameterEvent, mGpuRightOwner,
          mGspInterruptEvent, mGspSharedMemory, mDspSemaphoreEvent,
          mHidSharedMemory, mY2rCompletionEvent}) {
        registerObject(object);
    }
    for (const auto& type : mDspInterruptEvents) {
        for (const auto& object : type) {
            registerObject(object);
        }
    }
    for (const auto& object : mHidEvents) {
        registerObject(object);
    }
    for (const auto& object : mIrEvents) {
        registerObject(object);
    }
    registerObject(mIrSharedMemory);
    const auto objectId = [&](const std::shared_ptr<KernelObject>& object) {
        return object ? objectIds.at(object.get()) : 0U;
    };

    nlohmann::json encodedObjects = nlohmann::json::array();
    for (size_t index = 0; index < objects.size(); ++index) {
        const auto& object = *objects[index];
        encodedObjects.push_back({
            {"id", index + 1U},
            {"type", object.Type},
            {"thread_id", object.ThreadId.has_value()
                              ? nlohmann::json(*object.ThreadId)
                              : nlohmann::json(nullptr)},
            {"owner_thread_id", object.OwnerThreadId.has_value()
                                    ? nlohmann::json(*object.OwnerThreadId)
                                    : nlohmann::json(nullptr)},
            {"recursion_count", object.RecursionCount},
            {"available_count", object.AvailableCount},
            {"reset_type", object.ResetType},
            {"file_path", object.FilePath.string()},
            {"file_offset", object.FileOffset},
            {"file_size", object.FileSize},
            {"file_open_mode", object.FileOpenMode},
            {"file_priority", object.FilePriority},
            {"file_closed", object.FileClosed},
            {"mapped_address", object.MappedAddress.has_value()
                                   ? nlohmann::json(*object.MappedAddress)
                                   : nlohmann::json(nullptr)},
        });
    }
    nlohmann::json handles = nlohmann::json::array();
    for (const auto& handle : mHandles) {
        handles.push_back(
            {{"value", handle.Value}, {"object", objectId(handle.Object)}});
    }
    nlohmann::json arbiterWaiters = nlohmann::json::array();
    for (const auto& waiter : mArbiterWaiters) {
        arbiterWaiters.push_back({
            {"arbiter", objectId(waiter.Arbiter)},
            {"thread_id", waiter.ThreadId},
            {"address", waiter.Address},
        });
    }
    nlohmann::json synchronizationWaiters = nlohmann::json::array();
    for (const auto& waiter : mSynchronizationWaiters) {
        nlohmann::json waiterObjects = nlohmann::json::array();
        for (const auto& object : waiter.Objects) {
            waiterObjects.push_back(objectId(object));
        }
        synchronizationWaiters.push_back({
            {"objects", std::move(waiterObjects)},
            {"thread_id", waiter.ThreadId},
            {"wait_all", waiter.WaitAll},
        });
    }
    nlohmann::json sleepWaiters = nlohmann::json::array();
    for (const auto& waiter : mSleepWaiters) {
        sleepWaiters.push_back({
            {"thread_id", waiter.ThreadId},
            {"wake_tick", waiter.WakeTick},
            {"synchronization_timeout", waiter.SynchronizationTimeout},
        });
    }
    nlohmann::json archives = nlohmann::json::array();
    for (const auto& archive : mArchives) {
        archives.push_back({
            {"handle", archive.Handle},
            {"id", archive.Id},
            {"root", archive.Root.string()},
        });
    }
    nlohmann::json screens = nlohmann::json::array();
    for (const auto& screen : mGspScreens) {
        nlohmann::json slots = nlohmann::json::array();
        for (const auto& slot : screen.Slots) {
            slots.push_back({
                {"address_left", slot.AddressLeft},
                {"address_right", slot.AddressRight},
                {"stride", slot.Stride},
                {"format", slot.Format},
                {"configured", slot.Configured},
            });
        }
        screens.push_back({{"slots", std::move(slots)},
                           {"shown_buffer", screen.ShownBuffer}});
    }
    nlohmann::json dspInterruptEvents = nlohmann::json::array();
    for (const auto& type : mDspInterruptEvents) {
        nlohmann::json channels = nlohmann::json::array();
        for (const auto& object : type) {
            channels.push_back(objectId(object));
        }
        dspInterruptEvents.push_back(std::move(channels));
    }
    nlohmann::json dspPipeOutput = nlohmann::json::array();
    for (const auto& pipe : mDspPipeOutput) {
        dspPipeOutput.push_back(nlohmann::json::binary(pipe));
    }
    nlohmann::json hidEvents = nlohmann::json::array();
    for (const auto& object : mHidEvents) {
        hidEvents.push_back(objectId(object));
    }

    return {
        {"format", "oot3d_native_a32_ctr_host_state_v1"},
        {"kernel_objects", std::move(encodedObjects)},
        {"handles", std::move(handles)},
        {"arbiter_waiters", std::move(arbiterWaiters)},
        {"synchronization_waiters", std::move(synchronizationWaiters)},
        {"sleep_waiters", std::move(sleepWaiters)},
        {"archives", std::move(archives)},
        {"next_slot", mNextSlot},
        {"next_generation", mNextGeneration},
        {"fallback_pc", mFallbackPc},
        {"fallback_reason", static_cast<uint32_t>(mFallbackReason)},
        {"fallback_raw_operation", mFallbackRawOperation},
        {"linear_heap_cursor", mLinearHeapCursor},
        {"allocation_index", mAllocationIndex},
        {"next_archive_handle", mNextArchiveHandle},
        {"system_ticks", mSystemTicks},
        {"apt_lock", objectId(mAptLock)},
        {"apt_notification_event", objectId(mAptNotificationEvent)},
        {"apt_parameter_event", objectId(mAptParameterEvent)},
        {"gpu_right_owner", objectId(mGpuRightOwner)},
        {"gsp_interrupt_event", objectId(mGspInterruptEvent)},
        {"gsp_shared_memory", objectId(mGspSharedMemory)},
        {"gsp_thread_id", mGspThreadId},
        {"gsp_priority", mGspPriority},
        {"gsp_priority_with_rights", mGspPriorityWithRights},
        {"gsp_screens", std::move(screens)},
        {"lcd_force_black", mLcdForceBlack},
        {"apt_application_id", mAptApplicationId},
        {"apt_wakeup_pending", mAptWakeupPending},
        {"apt_screen_cap_post_permission", mAptScreenCapPostPermission},
        {"apt_library_applet", mAptLibraryApplet},
        {"apt_parameter",
         mAptParameter.has_value()
             ? nlohmann::json{{"sender", mAptParameter->Sender},
                              {"signal", mAptParameter->Signal},
                              {"buffer", nlohmann::json::binary(mAptParameter->Buffer)}}
             : nlohmann::json()},
        {"dsp_component", nlohmann::json::binary(mDspComponent)},
        {"dsp_program_mask", mDspProgramMask},
        {"dsp_data_mask", mDspDataMask},
        {"dsp_semaphore_mask", mDspSemaphoreMask},
        {"dsp_semaphore_value", mDspSemaphoreValue},
        {"dsp_audio_running", mDspAudioRunning},
        {"next_dsp_audio_frame_tick", mNextDspAudioFrameTick},
        {"pending_dsp_audio_frames", mPendingDspAudioFrames},
        {"dsp_interrupt_events", std::move(dspInterruptEvents)},
        {"dsp_semaphore_event", objectId(mDspSemaphoreEvent)},
        {"y2r_completion_event", objectId(mY2rCompletionEvent)},
        {"y2r", {{"input", mY2r.Input}, {"output", mY2r.Output},
                 {"rotation", mY2r.Rotation}, {"tiled", mY2r.Tiled},
                 {"width", mY2r.Width}, {"height", mY2r.Height},
                 {"alpha", mY2r.Alpha}, {"spatial_dither", mY2r.SpatialDither},
                 {"temporal_dither", mY2r.TemporalDither}, {"interrupt", mY2r.Interrupt},
                 {"coefficients", mY2r.Coefficients}, {"weights", mY2r.DitherWeights},
                 {"buffers", mY2r.Buffers}}},
        {"dsp_pipe_output", std::move(dspPipeOutput)},
        {"hid_shared_memory", objectId(mHidSharedMemory)},
        {"hid_events", std::move(hidEvents)},
        {"hid_runtime_profile",
         {{"shared_memory_mapped", mHidRuntimeProfile.SharedMemoryMapped},
          {"samples_written", mHidRuntimeProfile.SamplesWritten},
          {"accelerometer_samples_written",
           mHidRuntimeProfile.AccelerometerSamplesWritten},
          {"gyroscope_samples_written",
           mHidRuntimeProfile.GyroscopeSamplesWritten},
          {"event_batches_signaled", mHidRuntimeProfile.EventBatchesSignaled},
          {"last_sample_tick", mHidRuntimeProfile.LastSampleTick},
          {"last_buttons", mHidRuntimeProfile.LastButtons},
          {"last_additions", mHidRuntimeProfile.LastAdditions},
          {"last_removals", mHidRuntimeProfile.LastRemovals},
          {"last_circle_pad_x", mHidRuntimeProfile.LastCirclePadX},
          {"last_circle_pad_y", mHidRuntimeProfile.LastCirclePadY},
          {"last_accelerometer", mHidRuntimeProfile.LastAccelerometer},
          {"last_gyroscope", mHidRuntimeProfile.LastGyroscope},
          {"next_pad_index", mHidRuntimeProfile.NextPadIndex},
          {"next_accelerometer_index",
           mHidRuntimeProfile.NextAccelerometerIndex},
          {"next_gyroscope_index", mHidRuntimeProfile.NextGyroscopeIndex}}},
        {"next_hid_pad_tick", mNextHidPadTick},
        {"next_hid_accelerometer_tick", mNextHidAccelerometerTick},
        {"next_hid_gyroscope_tick", mNextHidGyroscopeTick},
        {"next_hid_pad_index", mNextHidPadIndex},
        {"next_hid_touch_index", mNextHidTouchIndex},
        {"next_hid_accelerometer_index", mNextHidAccelerometerIndex},
        {"next_hid_gyroscope_index", mNextHidGyroscopeIndex},
        {"hid_schedule_initialized", mHidScheduleInitialized},
        {"hid_accelerometer_schedule_initialized",
         mHidAccelerometerScheduleInitialized},
        {"hid_gyroscope_schedule_initialized",
         mHidGyroscopeScheduleInitialized},
        {"hid_accelerometer_enable_count", mHidAccelerometerEnableCount},
        {"hid_gyroscope_enable_count", mHidGyroscopeEnableCount},
        {"ir",
         {{"events", {objectId(mIrEvents[0]), objectId(mIrEvents[1]), objectId(mIrEvents[2])}},
          {"initialized", mIrInitialized},
          {"shared_memory", objectId(mIrSharedMemory)},
          {"receive_buffer_size", mIrReceiveBufferSize},
          {"receive_packet_capacity", mIrReceivePacketCapacity},
          {"receive_begin", mIrReceiveBegin},
          {"receive_end", mIrReceiveEnd},
          {"receive_count", mIrReceiveCount},
          {"extra_hid_attached", mIrExtraHidAttached},
          {"extra_hid_connected", mIrExtraHidConnected},
          {"extra_hid_period_ticks", mIrExtraHidPeriodTicks},
          {"next_extra_hid_report_tick", mIrNextExtraHidReportTick},
          {"extra_hid_reports_sent", mIrExtraHidReportsSent}}},
        {"ndm_scheduler_suspended", mNdmSchedulerSuspended},
        {"ndm_scheduler_runs_in_background", mNdmSchedulerRunsInBackground},
        {"ndm_default_daemons", mNdmDefaultDaemons},
        {"ndm_suspended_daemons", mNdmSuspendedDaemons},
        {"fs_priority", mFsPriority},
    };
}

bool NativeA32CtrHostServices::RestoreState(const nlohmann::json& state,
                                            NativeA32Process& process,
                                            std::string* error) {
    try {
        if (!state.is_object() || state.value("format", std::string{}) !=
                                      "oot3d_native_a32_ctr_host_state_v1") {
            if (error != nullptr) {
                *error = "native CTR host state format is invalid";
            }
            return false;
        }
        NativeA32CtrHostServices staged(mConfig);
        const auto normalizedAbsolute = [](const std::filesystem::path& path) {
            return std::filesystem::absolute(path).lexically_normal();
        };
        const auto pathIsWithin = [&](const std::filesystem::path& root,
                                      const std::filesystem::path& candidate) {
            if (root.empty() || candidate.empty()) {
                return false;
            }
            const auto normalizedRoot = normalizedAbsolute(root);
            const auto normalizedCandidate = normalizedAbsolute(candidate);
            if (normalizedRoot == normalizedCandidate) {
                return true;
            }
            const auto relative =
                normalizedCandidate.lexically_relative(normalizedRoot);
            return !relative.empty() && !relative.is_absolute() &&
                   *relative.begin() != "..";
        };
        const auto validFilePath = [&](const std::filesystem::path& path) {
            if (path.empty()) {
                return true;
            }
            return (!mConfig.RomFsImagePath.empty() &&
                    normalizedAbsolute(path) ==
                        normalizedAbsolute(mConfig.RomFsImagePath)) ||
                   pathIsWithin(mConfig.SaveDataDirectory, path);
        };
        std::vector<std::shared_ptr<KernelObject>> objects;
        const auto& encodedObjects = state.at("kernel_objects");
        objects.reserve(encodedObjects.size());
        for (size_t index = 0; index < encodedObjects.size(); ++index) {
            const auto& encoded = encodedObjects[index];
            if (encoded.at("id").get<size_t>() != index + 1U) {
                throw std::runtime_error("kernel object ID is invalid");
            }
            auto object = std::make_shared<KernelObject>();
            object->Type = encoded.at("type").get<std::string>();
            if (object->Type.empty()) {
                throw std::runtime_error("kernel object type is empty");
            }
            if (!encoded.at("thread_id").is_null()) {
                object->ThreadId = encoded.at("thread_id").get<uint32_t>();
            }
            if (!encoded.at("owner_thread_id").is_null()) {
                object->OwnerThreadId =
                    encoded.at("owner_thread_id").get<uint32_t>();
            }
            object->RecursionCount =
                encoded.at("recursion_count").get<uint32_t>();
            object->AvailableCount =
                encoded.at("available_count").get<int64_t>();
            object->ResetType = encoded.at("reset_type").get<uint32_t>();
            object->FilePath = std::filesystem::path(
                encoded.at("file_path").get<std::string>());
            const bool isRomFsFile =
                object->Type == "client_session:file:romfs";
            const bool isSaveFile =
                object->Type == "client_session:file:savedata";
            if (isRomFsFile) {
                if (mConfig.RomFsLayers == nullptr &&
                    mConfig.Filesystem == nullptr &&
                    mConfig.RomFsImagePath.empty()) {
                    throw std::runtime_error(
                        "RomFS kernel object has no configured image");
                }
                // Savestates may originate on another host OS. The serialized
                // RomFS path is an identity hint, not authority to open an
                // arbitrary host file; bind this object to the configured
                // process image before applying the normal path validation.
                object->FilePath =
                    mConfig.Filesystem != nullptr
                        ? std::filesystem::path(mConfig.RomFsContentPath)
                        : mConfig.RomFsImagePath;
            }
            if (mConfig.Filesystem == nullptr &&
                !validFilePath(object->FilePath)) {
                throw std::runtime_error(
                    "kernel object file path is incompatible");
            }
            object->FileOffset = encoded.at("file_offset").get<uint64_t>();
            object->FileSize = encoded.at("file_size").get<uint64_t>();
            object->FileOpenMode = encoded.at("file_open_mode").get<uint32_t>();
            object->FilePriority = encoded.value("file_priority", 0U);
            object->FileClosed = encoded.value("file_closed", false);
            if (isRomFsFile) {
                // The title caches the RomFS tables it read: a state is bound
                // to the view it was saved over (the image, installed RomFS
                // mods or development layers).
                const uint64_t expectedOffset =
                    mConfig.RomFsLayers != nullptr ? 0U : mConfig.RomFsImageOffset;
                if (object->FileOffset != expectedOffset || object->FileSize != RomFsServiceViewSize()) {
                    throw std::runtime_error(
                        "restored RomFS service view is incompatible (the save state "
                        "was made with different RomFS mods or game data)");
                }
            }
            if (!object->FileClosed && isRomFsFile && mConfig.RomFsLayers != nullptr) {
                object->FileBackend = mConfig.RomFsLayers;
            } else if (!object->FileClosed && mConfig.Filesystem != nullptr && (isRomFsFile || isSaveFile)) {
                uint32_t openFlags = 0U;
                if (isRomFsFile || (object->FileOpenMode & 1U) != 0U) {
                    openFlags |= NativeA32CtrFileOpenRead;
                }
                if (isSaveFile && (object->FileOpenMode & 2U) != 0U) {
                    openFlags |= NativeA32CtrFileOpenWrite;
                }
                if (!mConfig.Filesystem->Open(
                        isRomFsFile ? NativeA32CtrFilesystemRoot::Content
                                    : NativeA32CtrFilesystemRoot::Save,
                        object->FilePath.generic_string(), openFlags,
                        &object->FileBackend) ||
                    object->FileBackend == nullptr) {
                    throw std::runtime_error(
                        "kernel object backing file could not be restored");
                }
                if (isRomFsFile &&
                    (object->FileOffset > object->FileBackend->Size() ||
                     object->FileSize >
                         object->FileBackend->Size() - object->FileOffset)) {
                    throw std::runtime_error(
                        "restored RomFS range exceeds its backing file");
                }
            } else if (isRomFsFile && !object->FileClosed) {
                object->RomFsBacking = std::make_shared<RomFsBackingFile>(
                    object->FilePath, mConfig.RomFsOverlay,
                    mConfig.RomFsImageOffset);
            }
            if (!encoded.at("mapped_address").is_null()) {
                object->MappedAddress =
                    encoded.at("mapped_address").get<uint32_t>();
            }
            objects.push_back(std::move(object));
        }
        const auto object = [&](uint32_t id) -> std::shared_ptr<KernelObject> {
            if (id == 0U) {
                return {};
            }
            if (id > objects.size()) {
                throw std::runtime_error("kernel object reference is invalid");
            }
            return objects[id - 1U];
        };
        const auto validThread = [&](uint32_t threadId) {
            return process.ThreadState(threadId) != nullptr;
        };
        for (const auto& encoded : state.at("handles")) {
            auto referenced = object(encoded.at("object").get<uint32_t>());
            if (!referenced) {
                throw std::runtime_error("handle has no kernel object");
            }
            staged.mHandles.push_back(
                {encoded.at("value").get<uint32_t>(), std::move(referenced)});
        }
        for (const auto& encoded : state.at("arbiter_waiters")) {
            const uint32_t threadId = encoded.at("thread_id").get<uint32_t>();
            if (!validThread(threadId)) {
                throw std::runtime_error("arbiter waiter thread is invalid");
            }
            staged.mArbiterWaiters.push_back(
                {object(encoded.at("arbiter").get<uint32_t>()), &process,
                 threadId, encoded.at("address").get<uint32_t>()});
        }
        for (const auto& encoded : state.at("synchronization_waiters")) {
            SynchronizationWaiter waiter;
            waiter.Process = &process;
            waiter.ThreadId = encoded.at("thread_id").get<uint32_t>();
            waiter.WaitAll = encoded.at("wait_all").get<bool>();
            if (!validThread(waiter.ThreadId)) {
                throw std::runtime_error(
                    "synchronization waiter thread is invalid");
            }
            for (const auto& id : encoded.at("objects")) {
                auto referenced = object(id.get<uint32_t>());
                if (!referenced) {
                    throw std::runtime_error(
                        "synchronization waiter object is invalid");
                }
                waiter.Objects.push_back(std::move(referenced));
            }
            staged.mSynchronizationWaiters.push_back(std::move(waiter));
        }
        for (const auto& encoded : state.at("sleep_waiters")) {
            const uint32_t threadId = encoded.at("thread_id").get<uint32_t>();
            if (!validThread(threadId)) {
                throw std::runtime_error("sleep waiter thread is invalid");
            }
            staged.mSleepWaiters.push_back(
                {&process, threadId, encoded.at("wake_tick").get<uint64_t>(),
                 encoded.value("synchronization_timeout", false)});
        }
        for (const auto& encoded : state.at("archives")) {
            ArchiveRecord archive{
                encoded.at("handle").get<uint64_t>(),
                encoded.at("id").get<uint32_t>(),
                std::filesystem::path(encoded.at("root").get<std::string>())};
            const auto expectedRoot = mConfig.Filesystem != nullptr
                                          ? std::filesystem::path("save")
                                          : mConfig.SaveDataDirectory;
            if (archive.Root.lexically_normal() !=
                expectedRoot.lexically_normal()) {
                throw std::runtime_error("archive root is incompatible");
            }
            staged.mArchives.push_back(std::move(archive));
        }
        staged.mNextSlot = state.at("next_slot").get<uint16_t>();
        staged.mNextGeneration = state.at("next_generation").get<uint16_t>();
        if (staged.mNextGeneration == 0U ||
            staged.mNextGeneration >= (1U << 15U)) {
            throw std::runtime_error("handle generation is invalid");
        }
        staged.mFallbackPc = state.at("fallback_pc").get<uint32_t>();
        const uint32_t fallbackReason =
            state.at("fallback_reason").get<uint32_t>();
        if (fallbackReason >
            static_cast<uint32_t>(
                oot3d::recomp::a32::FallbackReason::MissingBlock)) {
            throw std::runtime_error("fallback reason is invalid");
        }
        staged.mFallbackReason =
            static_cast<oot3d::recomp::a32::FallbackReason>(fallbackReason);
        staged.mFallbackRawOperation =
            state.at("fallback_raw_operation").get<uint32_t>();
        staged.mLinearHeapCursor =
            state.at("linear_heap_cursor").get<uint32_t>();
        staged.mAllocationIndex = state.at("allocation_index").get<uint32_t>();
        staged.mNextArchiveHandle =
            state.at("next_archive_handle").get<uint64_t>();
        staged.mSystemTicks = state.at("system_ticks").get<uint64_t>();
        staged.mAptLock = object(state.at("apt_lock").get<uint32_t>());
        staged.mAptNotificationEvent =
            object(state.at("apt_notification_event").get<uint32_t>());
        staged.mAptParameterEvent =
            object(state.at("apt_parameter_event").get<uint32_t>());
        staged.mGpuRightOwner =
            object(state.at("gpu_right_owner").get<uint32_t>());
        staged.mGspInterruptEvent =
            object(state.at("gsp_interrupt_event").get<uint32_t>());
        staged.mGspSharedMemory =
            object(state.at("gsp_shared_memory").get<uint32_t>());
        staged.mGspThreadId = state.at("gsp_thread_id").get<uint32_t>();
        staged.mGspPriority = state.at("gsp_priority").get<uint32_t>();
        staged.mGspPriorityWithRights =
            state.at("gsp_priority_with_rights").get<uint32_t>();
        const auto& screens = state.at("gsp_screens");
        if (screens.size() != staged.mGspScreens.size()) {
            throw std::runtime_error("GSP screen count is invalid");
        }
        for (size_t screenIndex = 0; screenIndex < staged.mGspScreens.size();
             ++screenIndex) {
            const auto& encodedScreen = screens[screenIndex];
            const auto& slots = encodedScreen.at("slots");
            if (slots.size() != staged.mGspScreens[screenIndex].Slots.size()) {
                throw std::runtime_error(
                    "GSP framebuffer slot count is invalid");
            }
            for (size_t slotIndex = 0;
                 slotIndex < staged.mGspScreens[screenIndex].Slots.size();
                 ++slotIndex) {
                const auto& encodedSlot = slots[slotIndex];
                auto& slot = staged.mGspScreens[screenIndex].Slots[slotIndex];
                slot.AddressLeft =
                    encodedSlot.at("address_left").get<uint32_t>();
                slot.AddressRight =
                    encodedSlot.at("address_right").get<uint32_t>();
                slot.Stride = encodedSlot.at("stride").get<uint32_t>();
                slot.Format = encodedSlot.at("format").get<uint32_t>();
                slot.Configured = encodedSlot.at("configured").get<bool>();
            }
            staged.mGspScreens[screenIndex].ShownBuffer =
                encodedScreen.at("shown_buffer").get<uint32_t>();
            if (staged.mGspScreens[screenIndex].ShownBuffer >=
                staged.mGspScreens[screenIndex].Slots.size()) {
                throw std::runtime_error("GSP shown framebuffer is invalid");
            }
        }
        staged.mLcdForceBlack = state.at("lcd_force_black").get<bool>();
        staged.mAptApplicationId =
            state.at("apt_application_id").get<uint32_t>();
        staged.mAptWakeupPending = state.at("apt_wakeup_pending").get<bool>();
        // Absent from states saved before the command was modelled.
        staged.mAptScreenCapPostPermission =
            state.value("apt_screen_cap_post_permission", uint32_t{0});
        // Absent from states saved before library applets were modelled.
        staged.mAptLibraryApplet = state.value("apt_library_applet", uint32_t{0});
        if (const auto parameter = state.find("apt_parameter");
            parameter != state.end() && parameter->is_object()) {
            const auto& buffer = parameter->at("buffer");
            if (!buffer.is_binary()) {
                throw std::runtime_error("APT parameter state is invalid");
            }
            staged.mAptParameter = AptParameter{
                parameter->at("sender").get<uint32_t>(), parameter->at("signal").get<uint32_t>(),
                std::vector<uint8_t>(buffer.get_binary().begin(), buffer.get_binary().end())};
        }
        const auto& dspComponent = state.at("dsp_component");
        if (!dspComponent.is_binary()) {
            throw std::runtime_error("DSP component state is invalid");
        }
        staged.mDspComponent.assign(dspComponent.get_binary().begin(),
                                    dspComponent.get_binary().end());
        staged.mDspProgramMask = state.at("dsp_program_mask").get<uint16_t>();
        staged.mDspDataMask = state.at("dsp_data_mask").get<uint16_t>();
        staged.mDspSemaphoreMask =
            state.at("dsp_semaphore_mask").get<uint16_t>();
        staged.mDspSemaphoreValue =
            state.at("dsp_semaphore_value").get<uint16_t>();
        staged.mDspAudioRunning = state.at("dsp_audio_running").get<bool>();
        staged.mNextDspAudioFrameTick =
            state.at("next_dsp_audio_frame_tick").get<uint64_t>();
        staged.mPendingDspAudioFrames =
            state.at("pending_dsp_audio_frames").get<uint32_t>();
        const auto& dspInterrupts = state.at("dsp_interrupt_events");
        if (dspInterrupts.size() != staged.mDspInterruptEvents.size()) {
            throw std::runtime_error("DSP interrupt type count is invalid");
        }
        for (size_t type = 0; type < staged.mDspInterruptEvents.size();
             ++type) {
            if (dspInterrupts[type].size() !=
                staged.mDspInterruptEvents[type].size()) {
                throw std::runtime_error(
                    "DSP interrupt channel count is invalid");
            }
            for (size_t channel = 0;
                 channel < staged.mDspInterruptEvents[type].size(); ++channel) {
                staged.mDspInterruptEvents[type][channel] =
                    object(dspInterrupts[type][channel].get<uint32_t>());
            }
        }
        staged.mDspSemaphoreEvent =
            object(state.at("dsp_semaphore_event").get<uint32_t>());
        staged.mY2rCompletionEvent = object(state.value("y2r_completion_event", 0U));
        if (state.contains("y2r")) {
            const auto& y2r = state.at("y2r");
            auto& s = staged.mY2r;
            s.Input = y2r.at("input"); s.Output = y2r.at("output");
            s.Rotation = y2r.at("rotation"); s.Tiled = y2r.at("tiled");
            s.Width = y2r.at("width"); s.Height = y2r.at("height");
            s.Alpha = y2r.at("alpha"); s.SpatialDither = y2r.at("spatial_dither");
            s.TemporalDither = y2r.at("temporal_dither"); s.Interrupt = y2r.at("interrupt");
            s.Coefficients = y2r.at("coefficients").get<decltype(s.Coefficients)>();
            s.DitherWeights = y2r.at("weights").get<decltype(s.DitherWeights)>();
            s.Buffers = y2r.at("buffers").get<decltype(s.Buffers)>();
        }
        const auto& pipes = state.at("dsp_pipe_output");
        if (pipes.size() != staged.mDspPipeOutput.size()) {
            throw std::runtime_error("DSP pipe count is invalid");
        }
        for (size_t index = 0; index < staged.mDspPipeOutput.size(); ++index) {
            if (!pipes[index].is_binary()) {
                throw std::runtime_error("DSP pipe state is invalid");
            }
            staged.mDspPipeOutput[index].assign(
                pipes[index].get_binary().begin(),
                pipes[index].get_binary().end());
        }
        staged.mHidSharedMemory =
            object(state.at("hid_shared_memory").get<uint32_t>());
        const auto& hidEvents = state.at("hid_events");
        if (hidEvents.size() != staged.mHidEvents.size()) {
            throw std::runtime_error("HID event count is invalid");
        }
        for (size_t index = 0; index < staged.mHidEvents.size(); ++index) {
            staged.mHidEvents[index] = object(hidEvents[index].get<uint32_t>());
        }
        const auto& hid = state.at("hid_runtime_profile");
        staged.mHidRuntimeProfile.SharedMemoryMapped =
            hid.at("shared_memory_mapped").get<bool>();
        staged.mHidRuntimeProfile.SamplesWritten =
            hid.at("samples_written").get<uint64_t>();
        staged.mHidRuntimeProfile.AccelerometerSamplesWritten =
            hid.value("accelerometer_samples_written", 0ULL);
        staged.mHidRuntimeProfile.GyroscopeSamplesWritten =
            hid.value("gyroscope_samples_written", 0ULL);
        staged.mHidRuntimeProfile.EventBatchesSignaled =
            hid.at("event_batches_signaled").get<uint64_t>();
        staged.mHidRuntimeProfile.LastSampleTick =
            hid.at("last_sample_tick").get<uint64_t>();
        staged.mHidRuntimeProfile.LastButtons =
            hid.at("last_buttons").get<uint32_t>();
        staged.mHidRuntimeProfile.LastAdditions =
            hid.at("last_additions").get<uint32_t>();
        staged.mHidRuntimeProfile.LastRemovals =
            hid.at("last_removals").get<uint32_t>();
        staged.mHidRuntimeProfile.LastCirclePadX =
            hid.at("last_circle_pad_x").get<int16_t>();
        staged.mHidRuntimeProfile.LastCirclePadY =
            hid.at("last_circle_pad_y").get<int16_t>();
        if (hid.contains("last_accelerometer")) {
            staged.mHidRuntimeProfile.LastAccelerometer =
                hid.at("last_accelerometer").get<std::array<int16_t, 3>>();
        }
        if (hid.contains("last_gyroscope")) {
            staged.mHidRuntimeProfile.LastGyroscope =
                hid.at("last_gyroscope").get<std::array<int16_t, 3>>();
        }
        staged.mHidRuntimeProfile.NextPadIndex =
            hid.at("next_pad_index").get<uint32_t>();
        staged.mHidRuntimeProfile.NextAccelerometerIndex =
            hid.value("next_accelerometer_index", 0U);
        staged.mHidRuntimeProfile.NextGyroscopeIndex =
            hid.value("next_gyroscope_index", 0U);
        staged.mNextHidPadTick = state.at("next_hid_pad_tick").get<uint64_t>();
        staged.mNextHidAccelerometerTick =
            state.value("next_hid_accelerometer_tick", 0ULL);
        staged.mNextHidGyroscopeTick =
            state.value("next_hid_gyroscope_tick", 0ULL);
        staged.mNextHidPadIndex =
            state.at("next_hid_pad_index").get<uint32_t>();
        staged.mNextHidTouchIndex =
            state.at("next_hid_touch_index").get<uint32_t>();
        staged.mNextHidAccelerometerIndex =
            state.value("next_hid_accelerometer_index", 0U);
        staged.mNextHidGyroscopeIndex =
            state.value("next_hid_gyroscope_index", 0U);
        staged.mHidScheduleInitialized =
            state.at("hid_schedule_initialized").get<bool>();
        staged.mHidAccelerometerScheduleInitialized =
            state.value("hid_accelerometer_schedule_initialized", false);
        staged.mHidGyroscopeScheduleInitialized =
            state.value("hid_gyroscope_schedule_initialized", false);
        staged.mHidAccelerometerEnableCount =
            state.at("hid_accelerometer_enable_count").get<uint32_t>();
        staged.mHidGyroscopeEnableCount =
            state.at("hid_gyroscope_enable_count").get<uint32_t>();
        // States written before ir:USER was emulated have no "ir" entry.
        if (const auto ir = state.find("ir"); ir != state.end()) {
            const auto& events = ir->at("events");
            if (events.size() != staged.mIrEvents.size()) {
                throw std::runtime_error("ir:USER event count is invalid");
            }
            for (size_t index = 0; index < staged.mIrEvents.size(); ++index) {
                staged.mIrEvents[index] = object(events[index].get<uint32_t>());
            }
            staged.mIrInitialized = ir->at("initialized").get<bool>();
            staged.mIrSharedMemory = object(ir->at("shared_memory").get<uint32_t>());
            staged.mIrReceiveBufferSize = ir->at("receive_buffer_size").get<uint32_t>();
            staged.mIrReceivePacketCapacity = ir->at("receive_packet_capacity").get<uint32_t>();
            staged.mIrReceiveBegin = ir->at("receive_begin").get<uint32_t>();
            staged.mIrReceiveEnd = ir->at("receive_end").get<uint32_t>();
            staged.mIrReceiveCount = ir->at("receive_count").get<uint32_t>();
            staged.mIrExtraHidAttached = ir->at("extra_hid_attached").get<bool>();
            staged.mIrExtraHidConnected = ir->at("extra_hid_connected").get<bool>();
            staged.mIrExtraHidPeriodTicks = ir->at("extra_hid_period_ticks").get<uint64_t>();
            staged.mIrNextExtraHidReportTick = ir->at("next_extra_hid_report_tick").get<uint64_t>();
            staged.mIrExtraHidReportsSent = ir->at("extra_hid_reports_sent").get<uint64_t>();
        }
        staged.mNdmSchedulerSuspended =
            state.at("ndm_scheduler_suspended").get<bool>();
        staged.mNdmSchedulerRunsInBackground =
            state.at("ndm_scheduler_runs_in_background").get<bool>();
        // Absent from states saved before the command was modelled.
        staged.mNdmDefaultDaemons =
            state.value("ndm_default_daemons", kNdmAllDaemons);
        staged.mNdmSuspendedDaemons =
            state.value("ndm_suspended_daemons", uint32_t{0});
        staged.mFsPriority = state.value("fs_priority", uint32_t{0});
        if (((staged.mNdmDefaultDaemons | staged.mNdmSuspendedDaemons) &
             ~kNdmAllDaemons) != 0U) {
            throw std::runtime_error("NDM daemon mask is invalid");
        }

        staged.mRuntimeProfile.FsUnservedRequests = mRuntimeProfile.FsUnservedRequests;
        *this = std::move(staged);
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = std::string("native CTR host state decode failed: ") +
                     exception.what();
        }
        return false;
    }
}

} // namespace Oot3dNativeGame
