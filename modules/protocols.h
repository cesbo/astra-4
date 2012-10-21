#ifndef _PROTOCOLS_H_
#define _PROTOCOLS_H_ 1

#ifndef _ASTRA_H_
typedef struct list_s list_t;
typedef struct module_data_s module_data_t;
#endif

#define ASTRA_STREAM_TS_API protocol_stream_ts_t *stream_ts;
#define ASTRA_SOFTCAM_API protocol_softcam_t *softcam;
#define ASTRA_MODULE_EVENT_API protocol_module_event_t *module_event;

#ifdef ASTRA_STREAM_TS_API

typedef struct protocol_stream_ts_s protocol_stream_ts_t;

typedef void (*stream_ts_send_ts_t)(module_data_t *, unsigned char *);

typedef void (*stream_ts_on_attach_t)(module_data_t *, module_data_t *);
typedef stream_ts_on_attach_t stream_ts_on_detach_t;

typedef void (*stream_ts_join_pid_t)(module_data_t *, module_data_t *
                                          , unsigned short);
typedef stream_ts_join_pid_t stream_ts_leave_pid_t;

void stream_ts_init(module_data_t *, stream_ts_send_ts_t
                    , stream_ts_on_attach_t, stream_ts_on_detach_t
                    , stream_ts_join_pid_t, stream_ts_leave_pid_t);
void stream_ts_destroy(module_data_t *);
void stream_ts_send(module_data_t *, unsigned char *);
void stream_ts_sendto(module_data_t *, unsigned char *);
void stream_ts_attach(module_data_t *);
void stream_ts_detach(module_data_t *);

void stream_ts_join_pid(module_data_t *, unsigned short);
void stream_ts_leave_pid(module_data_t *, unsigned short);
void stream_ts_leave_all(module_data_t *);

#endif /* ASTRA_STREAM_TS_API */

#include <modules/module_event.h>

#define ASTRA_PROTOCOLS                                                     \
    ASTRA_STREAM_TS_API

#endif /* _PROTOCOLS_H_ */
