#ifndef TRIAEVUM_SERVICE_ABI_H
#define TRIAEVUM_SERVICE_ABI_H

#include "triaevum/module_abi.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  TRIAEVUM_SERVICE_SCHEMA_V1 = 1,

  /* FourCC values are written in little-endian byte order. */
  TRIAEVUM_SERVICE_KERNEL_V1 = 0x4E52454BU,     /* KERN */
  TRIAEVUM_SERVICE_PICA_V1 = 0x41434950U,       /* PICA */
  TRIAEVUM_SERVICE_AUDIO_V1 = 0x49445541U,      /* AUDI */
  TRIAEVUM_SERVICE_FILESYSTEM_V1 = 0x53595346U, /* FSYS */
  TRIAEVUM_SERVICE_INPUT_V1 = 0x33444948U,      /* HID3 */
  TRIAEVUM_SERVICE_SCHEDULER_V1 = 0x44484353U,  /* SCHD */
};

typedef struct TriAevumServiceRequestHeaderV1 {
  uint32_t struct_size;
  uint32_t schema_version;
} TriAevumServiceRequestHeaderV1;

typedef struct TriAevumServiceResponseHeaderV1 {
  uint32_t struct_size;
  uint32_t schema_version;
} TriAevumServiceResponseHeaderV1;

typedef struct TriAevumPayloadRangeV1 {
  uint32_t offset;
  uint32_t size;
} TriAevumPayloadRangeV1;

typedef uint32_t TriAevumKernelOperationV1;
enum {
  TRIAEVUM_KERNEL_DISPATCH_SVC_V1 = 1,
};

typedef struct TriAevumKernelSvcRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  uint32_t svc_number;
  uint32_t argument_count;
  uint64_t arguments[8];
} TriAevumKernelSvcRequestV1;

typedef struct TriAevumKernelSvcResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
  int32_t result;
  uint32_t output_count;
  uint64_t outputs[8];
} TriAevumKernelSvcResponseV1;

typedef uint32_t TriAevumPicaOperationV1;
enum {
  TRIAEVUM_PICA_WRITE_REGISTERS_V1 = 1,
  TRIAEVUM_PICA_SUBMIT_GSP_COMMAND_V1 = 2,
  TRIAEVUM_PICA_SET_FRAMEBUFFER_V1 = 3,
  TRIAEVUM_PICA_SET_LCD_FORCE_BLACK_V1 = 4,
  TRIAEVUM_PICA_TAKE_INTERRUPTS_V1 = 5,
  TRIAEVUM_PICA_FLUSH_V1 = 6,
};

typedef struct TriAevumPicaWriteRegistersRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  uint32_t base_register;
  uint32_t register_count;
  TriAevumPayloadRangeV1 values;
  TriAevumPayloadRangeV1 masks;
} TriAevumPicaWriteRegistersRequestV1;

typedef struct TriAevumPicaWriteRegistersResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
} TriAevumPicaWriteRegistersResponseV1;

typedef struct TriAevumPicaSubmitGspCommandRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  uint64_t frame_sequence;
  uint32_t control;
  uint32_t parameters[7];
  uint32_t command_word_count;
  uint32_t reserved;
  TriAevumPayloadRangeV1 command_words;
} TriAevumPicaSubmitGspCommandRequestV1;

typedef uint32_t TriAevumPicaSubmissionFlagsV1;
enum {
  TRIAEVUM_PICA_DISPLAY_TRANSFER_DEFERRED_V1 = 1U << 0U,
  TRIAEVUM_PICA_MEMORY_FILL_DEFERRED_V1 = 1U << 1U,
  TRIAEVUM_PICA_DISPLAY_TRANSFER_CPU_COPY_SUPPRESSED_V1 = 1U << 2U,
};

typedef struct TriAevumPicaSubmitGspCommandResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
  uint64_t submission_id;
  TriAevumPicaSubmissionFlagsV1 flags;
  uint32_t reserved;
} TriAevumPicaSubmitGspCommandResponseV1;

typedef struct TriAevumPicaSetFramebufferRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  uint32_t screen;
  uint32_t active_buffer;
  uint32_t address_left;
  uint32_t address_right;
  uint32_t stride;
  uint32_t format;
  uint32_t shown_buffer;
  uint32_t reserved;
} TriAevumPicaSetFramebufferRequestV1;

typedef struct TriAevumPicaSetFramebufferResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
} TriAevumPicaSetFramebufferResponseV1;

typedef struct TriAevumPicaSetLcdForceBlackRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  uint32_t force_black;
  uint32_t reserved;
} TriAevumPicaSetLcdForceBlackRequestV1;

typedef struct TriAevumPicaSetLcdForceBlackResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
} TriAevumPicaSetLcdForceBlackResponseV1;

typedef struct TriAevumPicaTakeInterruptsRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
} TriAevumPicaTakeInterruptsRequestV1;

typedef struct TriAevumPicaTakeInterruptsResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
  uint32_t interrupt_count;
  uint32_t reserved;
  TriAevumPayloadRangeV1 interrupts;
} TriAevumPicaTakeInterruptsResponseV1;

typedef struct TriAevumPicaFlushRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  uint64_t frame_sequence;
} TriAevumPicaFlushRequestV1;

typedef struct TriAevumPicaFlushResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
  uint64_t completed_submission_id;
} TriAevumPicaFlushResponseV1;

typedef uint32_t TriAevumAudioOperationV1;
enum {
  TRIAEVUM_AUDIO_SUBMIT_PCM_V1 = 1,
  TRIAEVUM_AUDIO_SET_STREAM_STATE_V1 = 2,
};

typedef uint32_t TriAevumAudioSampleFormatV1;
enum {
  TRIAEVUM_AUDIO_S16_V1 = 1,
  TRIAEVUM_AUDIO_F32_V1 = 2,
};

typedef uint32_t TriAevumAudioStreamStateV1;
enum {
  TRIAEVUM_AUDIO_STREAM_STOPPED_V1 = 0,
  TRIAEVUM_AUDIO_STREAM_PLAYING_V1 = 1,
  TRIAEVUM_AUDIO_STREAM_PAUSED_V1 = 2,
};

typedef struct TriAevumAudioSubmitPcmRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  uint32_t stream_id;
  TriAevumAudioSampleFormatV1 sample_format;
  uint32_t channel_count;
  uint32_t sample_rate_hz;
  uint32_t frame_count;
  uint32_t reserved;
  TriAevumPayloadRangeV1 samples;
} TriAevumAudioSubmitPcmRequestV1;

typedef struct TriAevumAudioSubmitPcmResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
  uint32_t accepted_frame_count;
  uint32_t queued_frame_count;
} TriAevumAudioSubmitPcmResponseV1;

typedef struct TriAevumAudioSetStreamStateRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  uint32_t stream_id;
  TriAevumAudioStreamStateV1 state;
} TriAevumAudioSetStreamStateRequestV1;

typedef struct TriAevumAudioSetStreamStateResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
} TriAevumAudioSetStreamStateResponseV1;

typedef uint32_t TriAevumFilesystemOperationV1;
enum {
  TRIAEVUM_FILESYSTEM_OPEN_V1 = 1,
  TRIAEVUM_FILESYSTEM_READ_V1 = 2,
  TRIAEVUM_FILESYSTEM_WRITE_V1 = 3,
  TRIAEVUM_FILESYSTEM_CLOSE_V1 = 4,
  TRIAEVUM_FILESYSTEM_STAT_V1 = 5,
  TRIAEVUM_FILESYSTEM_RESIZE_V1 = 6,
};

typedef uint32_t TriAevumFilesystemRootV1;
enum {
  TRIAEVUM_FILESYSTEM_CONTENT_V1 = 1,
  TRIAEVUM_FILESYSTEM_SAVE_V1 = 2,
};

typedef uint32_t TriAevumFilesystemOpenFlagsV1;
enum {
  TRIAEVUM_FILESYSTEM_OPEN_READ_V1 = 1U << 0U,
  TRIAEVUM_FILESYSTEM_OPEN_WRITE_V1 = 1U << 1U,
  TRIAEVUM_FILESYSTEM_OPEN_CREATE_V1 = 1U << 2U,
  TRIAEVUM_FILESYSTEM_OPEN_TRUNCATE_V1 = 1U << 3U,
};

typedef struct TriAevumFilesystemOpenRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  TriAevumFilesystemRootV1 root;
  TriAevumFilesystemOpenFlagsV1 flags;
  TriAevumPayloadRangeV1 utf8_path;
} TriAevumFilesystemOpenRequestV1;

typedef struct TriAevumFilesystemOpenResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
  uint64_t handle;
  uint64_t size;
} TriAevumFilesystemOpenResponseV1;

typedef struct TriAevumFilesystemReadRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  uint64_t handle;
  uint64_t offset;
  uint32_t requested_size;
  uint32_t reserved;
} TriAevumFilesystemReadRequestV1;

typedef struct TriAevumFilesystemReadResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
  uint32_t returned_size;
  uint32_t end_of_file;
  TriAevumPayloadRangeV1 data;
} TriAevumFilesystemReadResponseV1;

typedef struct TriAevumFilesystemWriteRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  uint64_t handle;
  uint64_t offset;
  TriAevumPayloadRangeV1 data;
} TriAevumFilesystemWriteRequestV1;

typedef struct TriAevumFilesystemWriteResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
  uint32_t written_size;
  uint32_t reserved;
} TriAevumFilesystemWriteResponseV1;

typedef struct TriAevumFilesystemCloseRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  uint64_t handle;
} TriAevumFilesystemCloseRequestV1;

typedef struct TriAevumFilesystemCloseResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
} TriAevumFilesystemCloseResponseV1;

typedef struct TriAevumFilesystemStatRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  TriAevumFilesystemRootV1 root;
  uint32_t reserved;
  TriAevumPayloadRangeV1 utf8_path;
} TriAevumFilesystemStatRequestV1;

typedef struct TriAevumFilesystemStatResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
  uint64_t size;
  uint64_t modified_time_ns;
  uint32_t flags;
  uint32_t reserved;
} TriAevumFilesystemStatResponseV1;

typedef uint32_t TriAevumFilesystemStatFlagsV1;
enum {
  TRIAEVUM_FILESYSTEM_STAT_REGULAR_FILE_V1 = 1U << 0U,
  TRIAEVUM_FILESYSTEM_STAT_DIRECTORY_V1 = 1U << 1U,
};

typedef struct TriAevumFilesystemResizeRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  uint64_t handle;
  uint64_t size;
} TriAevumFilesystemResizeRequestV1;

typedef struct TriAevumFilesystemResizeResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
} TriAevumFilesystemResizeResponseV1;

typedef uint32_t TriAevumInputOperationV1;
enum {
  TRIAEVUM_INPUT_READ_STATE_V1 = 1,
};

/*
 * The abstract 3DS control surface has exactly one route for every native
 * digital channel. Values intentionally match the native HID bit layout so a
 * game module can project them without a title-specific host convention.
 */
typedef uint64_t TriAevumInputButtonsV1;
enum {
  TRIAEVUM_INPUT_BUTTON_A_V1 = 1U << 0U,
  TRIAEVUM_INPUT_BUTTON_B_V1 = 1U << 1U,
  TRIAEVUM_INPUT_BUTTON_SELECT_V1 = 1U << 2U,
  TRIAEVUM_INPUT_BUTTON_START_V1 = 1U << 3U,
  TRIAEVUM_INPUT_BUTTON_DPAD_RIGHT_V1 = 1U << 4U,
  TRIAEVUM_INPUT_BUTTON_DPAD_LEFT_V1 = 1U << 5U,
  TRIAEVUM_INPUT_BUTTON_DPAD_UP_V1 = 1U << 6U,
  TRIAEVUM_INPUT_BUTTON_DPAD_DOWN_V1 = 1U << 7U,
  TRIAEVUM_INPUT_BUTTON_R_V1 = 1U << 8U,
  TRIAEVUM_INPUT_BUTTON_L_V1 = 1U << 9U,
  TRIAEVUM_INPUT_BUTTON_X_V1 = 1U << 10U,
  TRIAEVUM_INPUT_BUTTON_Y_V1 = 1U << 11U,
  TRIAEVUM_INPUT_BUTTON_DEBUG_V1 = 1U << 12U,
  TRIAEVUM_INPUT_BUTTON_GPIO14_V1 = 1U << 13U,
  TRIAEVUM_INPUT_BUTTON_ZL_V1 = 1U << 14U,
  TRIAEVUM_INPUT_BUTTON_ZR_V1 = 1U << 15U,
  TRIAEVUM_INPUT_BUTTON_MASK_V1 = 0xFFFFU,
};

typedef struct TriAevumInputReadStateRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  uint32_t player_index;
  uint32_t reserved;
} TriAevumInputReadStateRequestV1;

typedef uint32_t TriAevumInputStateFlagsV1;
enum {
  TRIAEVUM_INPUT_TOUCH_VALID_V1 = 1U << 0U,
  TRIAEVUM_INPUT_GYROSCOPE_VALID_V1 = 1U << 1U,
  TRIAEVUM_INPUT_ACCELEROMETER_VALID_V1 = 1U << 2U,
  TRIAEVUM_INPUT_TOUCH_PRESSED_V1 = 1U << 3U,
};

typedef struct TriAevumInputReadStateResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
  uint64_t sample_sequence;
  uint64_t buttons;
  float left_stick_x;
  float left_stick_y;
  float right_stick_x;
  float right_stick_y;
  float touch_x;
  float touch_y;
  float gyroscope_x;
  float gyroscope_y;
  float gyroscope_z;
  float accelerometer_x;
  float accelerometer_y;
  float accelerometer_z;
  TriAevumInputStateFlagsV1 flags;
  uint32_t reserved;
} TriAevumInputReadStateResponseV1;

typedef uint32_t TriAevumSchedulerOperationV1;
enum {
  TRIAEVUM_SCHEDULER_YIELD_V1 = 1,
};

typedef uint32_t TriAevumSchedulerYieldReasonV1;
enum {
  TRIAEVUM_SCHEDULER_YIELD_COOPERATIVE_V1 = 1,
  TRIAEVUM_SCHEDULER_YIELD_WAIT_EVENT_V1 = 2,
  TRIAEVUM_SCHEDULER_YIELD_WAIT_DEADLINE_V1 = 3,
};

typedef struct TriAevumSchedulerYieldRequestV1 {
  TriAevumServiceRequestHeaderV1 header;
  uint64_t thread_token;
  uint64_t deadline_ns;
  TriAevumSchedulerYieldReasonV1 reason;
  uint32_t reserved;
} TriAevumSchedulerYieldRequestV1;

typedef struct TriAevumSchedulerYieldResponseV1 {
  TriAevumServiceResponseHeaderV1 header;
  uint64_t resume_time_ns;
  uint32_t interrupted;
  uint32_t reserved;
} TriAevumSchedulerYieldResponseV1;

#ifdef __cplusplus
}
#endif

#endif
