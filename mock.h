#ifndef PLAB_TEST_MOCK_H
#define PLAB_TEST_MOCK_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define UNUSED(x) (void)(x)
#define RECORD_GUI "gui"
#define RECORD_STORAGE "storage"
#define RECORD_EXPANSION "expansion"
typedef struct FuriMessageQueue FuriMessageQueue;
typedef struct ViewPort ViewPort;
typedef struct Gui Gui;
typedef struct Canvas Canvas;
typedef struct Storage Storage;
typedef struct File File;
typedef struct Expansion Expansion;
typedef struct FuriHalAdcHandle FuriHalAdcHandle;
typedef struct FuriHalSerialHandle FuriHalSerialHandle;
typedef struct { int stub; } GpioPin;
extern const GpioPin gpio_ext_pc0, gpio_ext_pc3;
typedef enum {FuriStatusOk} FuriStatus;
typedef enum {InputTypeShort,InputTypeRepeat} InputType;
typedef enum {InputKeyBack,InputKeyLeft,InputKeyRight,InputKeyUp,InputKeyDown,InputKeyOk} InputKey;
typedef struct { InputKey key; InputType type; } InputEvent;
typedef enum {FontPrimary,FontSecondary,FontBigNumbers} Font;
typedef enum {ColorWhite,ColorBlack} Color;
typedef enum {AlignLeft,AlignRight,AlignBottom} Align;
typedef enum {GuiLayerFullscreen} GuiLayer;
typedef enum {FSAM_WRITE,FSAM_READ} FSAccess;
typedef enum {FSOM_OPEN_APPEND,FSOM_OPEN_EXISTING,FSOM_CREATE_ALWAYS} FSOpen;
typedef enum {FuriHalSubGhzPathIsolate} FuriHalSubGhzPath;
typedef enum {GpioModeInput,GpioModeAnalog} GpioMode;
typedef enum {GpioPullDown,GpioPullNo} GpioPull;
typedef enum {GpioSpeedLow} GpioSpeed;
typedef enum {FuriHalAdcScale2500} FuriHalAdcScale;
typedef enum {FuriHalAdcClockSync64} FuriHalAdcClock;
typedef enum {FuriHalAdcOversample64} FuriHalAdcOversample;
typedef enum {FuriHalAdcSamplingtime247_5} FuriHalAdcSamplingTime;
typedef enum {FuriHalAdcChannel1} FuriHalAdcChannel;
typedef enum {FuriHalSerialIdUsart} FuriHalSerialId;
typedef enum {FuriHalSerialRxEventData=1} FuriHalSerialRxEvent;
void* furi_record_open(const char*);
void furi_record_close(const char*);
uint32_t furi_get_tick(void);
FuriMessageQueue* furi_message_queue_alloc(size_t,size_t);
void furi_message_queue_free(FuriMessageQueue*);
FuriStatus furi_message_queue_get(FuriMessageQueue*,void*,uint32_t);
FuriStatus furi_message_queue_put(FuriMessageQueue*,const void*,uint32_t);
ViewPort* view_port_alloc(void);
void view_port_free(ViewPort*);
void view_port_draw_callback_set(ViewPort*,void(*)(Canvas*,void*),void*);
void view_port_input_callback_set(ViewPort*,void(*)(InputEvent*,void*),void*);
void view_port_update(ViewPort*);
void gui_add_view_port(Gui*,ViewPort*,GuiLayer);
void gui_remove_view_port(Gui*,ViewPort*);
void canvas_clear(Canvas*);
void canvas_set_font(Canvas*,Font);
void canvas_set_color(Canvas*,Color);
void canvas_draw_str(Canvas*,int,int,const char*);
void canvas_draw_str_aligned(Canvas*,int,int,Align,Align,const char*);
void canvas_draw_box(Canvas*,int,int,int,int);
void canvas_draw_frame(Canvas*,int,int,int,int);
void canvas_draw_line(Canvas*,int,int,int,int);
uint8_t furi_hal_power_get_pct(void);
bool furi_hal_power_is_charging(void);
const char* furi_hal_version_get_name_ptr(void);
void furi_hal_subghz_idle(void);
bool furi_hal_subghz_is_frequency_valid(uint32_t);
uint32_t furi_hal_subghz_set_frequency_and_path(uint32_t);
void furi_hal_subghz_rx(void);
float furi_hal_subghz_get_rssi(void);
void furi_hal_subghz_reset(void);
void furi_hal_subghz_load_custom_preset(const uint8_t*);
void furi_hal_subghz_set_path(FuriHalSubGhzPath);
void furi_hal_subghz_sleep(void);
void furi_hal_gpio_init(const GpioPin*,GpioMode,GpioPull,GpioSpeed);
bool furi_hal_gpio_read(const GpioPin*);
FuriHalAdcHandle* furi_hal_adc_acquire(void);
void furi_hal_adc_configure_ex(FuriHalAdcHandle*,FuriHalAdcScale,FuriHalAdcClock,FuriHalAdcOversample,FuriHalAdcSamplingTime);
void furi_hal_adc_release(FuriHalAdcHandle*);
uint16_t furi_hal_adc_read(FuriHalAdcHandle*,FuriHalAdcChannel);
float furi_hal_adc_convert_to_voltage(FuriHalAdcHandle*,uint16_t);
void expansion_disable(Expansion*);
void expansion_enable(Expansion*);
FuriHalSerialHandle* furi_hal_serial_control_acquire(FuriHalSerialId);
void furi_hal_serial_control_release(FuriHalSerialHandle*);
void furi_hal_serial_init(FuriHalSerialHandle*,uint32_t);
void furi_hal_serial_deinit(FuriHalSerialHandle*);
void furi_hal_serial_async_rx_start(FuriHalSerialHandle*,void(*)(FuriHalSerialHandle*,FuriHalSerialRxEvent,void*),void*,bool);
void furi_hal_serial_async_rx_stop(FuriHalSerialHandle*);
bool furi_hal_serial_async_rx_available(FuriHalSerialHandle*);
uint8_t furi_hal_serial_async_rx(FuriHalSerialHandle*);
void furi_hal_serial_tx(FuriHalSerialHandle*,const uint8_t*,size_t);
void storage_common_mkdir(Storage*,const char*);
File* storage_file_alloc(Storage*);
bool storage_file_open(File*,const char*,FSAccess,FSOpen);
size_t storage_file_write(File*,const void*,size_t);
size_t storage_file_read(File*,void*,size_t);
bool storage_file_close(File*);
void storage_file_free(File*);
#endif
