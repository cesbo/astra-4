
#include <astra.h>

#include <modules/mpegts/mpegts.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#define LOG_MSG(_msg) "[asi_input %d] " _msg, mod->config.adapter

struct module_data_s
{
    MODULE_BASE();

    struct
    {
        int adapter;
        int budget;
    } config;

    int fd;
    uint8_t filter[MAX_PID / 8];
};

#define ASI_IOC_RXSETPF _IOW('?', 76, unsigned int [256])


static void asi_read_callback(void *arg, int event)
{
    module_data_t *mod = arg;

    if(event == EVENT_ERROR)
    {
        event_detach(mod->fd);
        close(mod->fd);
        mod->fd = 0;
        return;
    }

    // TODO: continue here ...
}

static void callback_join_pid(module_data_t *mod
                              , module_data_t *child
                              , uint16_t pid)
{
    if(mod->config.budget)
        return;

    mod->filter[pid / 8] |= (0x01 << (pid % 8));
    if(ioctl(mod->fd, ASI_IOC_RXSETPF, mod->filter) < 0)
    {
        log_error(LOG_MSG("failed to set PES filter [%s]")
                  , strerror(errno));
    }
}

static void callback_leave_pid(module_data_t *mod
                               , module_data_t *child
                               , uint16_t pid)
{
    if(mod->config.budget)
        return;

    mod->filter[pid / 8] &= ~(0x01 << (pid % 8));
    if(ioctl(mod->fd, ASI_IOC_RXSETPF, mod->filter) < 0)
    {
        log_error(LOG_MSG("failed to set PES filter [%s]")
                  , strerror(errno));
    }
}

static int method_attach(module_data_t *mod)
{
    stream_ts_attach(mod);
    return 0;
}

static int method_detach(module_data_t *mod)
{
    stream_ts_detach(mod);
    return 0;
}

static void module_initialize(module_data_t *mod)
{
    stream_ts_init(mod, NULL, NULL, NULL
                   , callback_join_pid, callback_leave_pid);

    module_set_number(mod, "adapter", 1, 0, &mod->config.adapter);
    module_set_number(mod, "budget", 0, 0, &mod->config.budget);

    char dev_name[16];
    sprintf(dev_name, "/dev/asirx%d", mod->config.adapter);
    mod->fd = open(dev_name, O_RDONLY);
    if(mod->fd <= 0)
    {
        log_error(LOG_MSG("failed to open device %s [%s]")
                  , dev_name, strerror(errno));
        mod->fd = 0;
        return;
    }

    if(!mod->config.budget)
    { /* hw filtering */
        memset(mod->filter, 0x00, sizeof(mod->filter));
    }
    else
    {
        memset(mod->filter, 0xFF, sizeof(mod->filter));
        mod->filter[NULL_TS_PID / 8] &= ~(0x01 << (NULL_TS_PID % 8));
    }
    if(ioctl(mod->fd, ASI_IOC_RXSETPF, mod->filter) < 0)
    {
        log_error(LOG_MSG("failed to set PES filter [%s]")
                  , strerror(errno));
    }

    fsync(mod->fd);

    event_attach(mod->fd, asi_read_callback, mod, EVENT_READ);
}

static void module_destroy(module_data_t *mod)
{
    stream_ts_destroy(mod);

    if(mod->fd)
    {
        event_detach(mod->fd);
        close(mod->fd);
    }
}

MODULE_METHODS()
{
    METHOD(attach)
    METHOD(detach)
};

MODULE(asi_input)
