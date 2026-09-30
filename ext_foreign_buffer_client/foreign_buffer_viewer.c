/*
 * mir-foreign-buffer-viewer
 *
 * A raw-Wayland test client for ext_foreign_buffer_v1.
 *
 * It opens a single toplevel window and tiles every capture source into it, asking the
 * compositor to render each source directly into a compositor-owned ("foreign") buffer.
 * The client never sees or touches the pixels.
 *
 *   --toplevels  (default)  one tile per ext_foreign_toplevel_handle_v1
 *   --outputs               one tile per wl_output
 *
 * This is throwaway test scaffolding; it is not part of the Mir source tree.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#include "ext-foreign-buffer-v1-client-protocol.h"
#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define SELF_APP_ID "io.mir_server.ForeignBufferViewer"

enum capture_mode
{
    MODE_TOPLEVELS,
    MODE_OUTPUTS,
};

struct viewer;

struct rect
{
    int x, y, w, h;
};

/* One captured source, displayed on its own subsurface. */
struct tile
{
    struct wl_list link;
    struct viewer* viewer;
    char* label;

    struct ext_image_capture_source_v1* source;
    struct ext_foreign_buffer_session_v1* session;
    struct wl_buffer* buffer;

    struct wl_surface* surface;
    struct wl_subsurface* subsurface;
    struct wp_viewport* viewport;

    uint32_t buffer_width, buffer_height;
    uint32_t logical_width, logical_height;

    bool have_size;   /* a size event has arrived, so the tile can be laid out */
    bool need_attach; /* the current wl_buffer has not been attached yet */
    bool stopped;

    struct rect slot;
};

struct toplevel
{
    struct wl_list link;
    struct viewer* viewer;
    struct ext_foreign_toplevel_handle_v1* handle;
    char* title;
    char* app_id;
    struct tile* tile;
    bool info_done;
    bool started;
};

struct output
{
    struct wl_list link;
    struct viewer* viewer;
    struct wl_output* output;
    uint32_t global_name;
    char* name;
    struct tile* tile;
    bool info_done;
    bool started;
};

struct viewer
{
    struct wl_display* display;
    struct wl_registry* registry;

    struct wl_compositor* compositor;
    struct wl_subcompositor* subcompositor;
    struct wl_shm* shm;
    struct wp_viewporter* viewporter;
    struct xdg_wm_base* wm_base;

    struct ext_foreign_buffer_manager_v1* foreign_buffer_manager;
    struct ext_output_image_capture_source_manager_v1* output_source_manager;
    struct ext_foreign_toplevel_image_capture_source_manager_v1* toplevel_source_manager;
    struct ext_foreign_toplevel_list_v1* toplevel_list;

    struct wl_surface* surface;
    struct xdg_surface* xdg_surface;
    struct xdg_toplevel* xdg_toplevel;
    struct wl_region* empty_region;

    struct wl_buffer* bg_buffer;
    uint32_t* bg_data;
    size_t bg_size;
    int bg_width, bg_height;

    int width, height;
    bool configured;
    bool running;
    bool layout_dirty;
    /* Sources announced during the initial roundtrips must wait for the window to exist. */
    bool ready;

    enum capture_mode mode;
    char const* output_filter;
    bool include_self;

    struct wl_list tiles;
    struct wl_list toplevels;
    struct wl_list outputs;
};

static void relayout(struct viewer* viewer);
static void toplevel_maybe_start(struct toplevel* toplevel);
static void output_maybe_start(struct output* output);

static void schedule_layout(struct viewer* viewer)
{
    viewer->layout_dirty = true;
}

static char* xstrdup(char const* s)
{
    char* copy = strdup(s ? s : "");
    if (!copy)
    {
        fprintf(stderr, "out of memory\n");
        abort();
    }
    return copy;
}

/* ------------------------------------------------------------------ shm --- */

static int create_shm_file(size_t size)
{
    int const fd = memfd_create("foreign-buffer-viewer", MFD_CLOEXEC);
    if (fd < 0)
    {
        return -1;
    }

    if (ftruncate(fd, (off_t)size) < 0)
    {
        close(fd);
        return -1;
    }

    return fd;
}

static bool background_realloc(struct viewer* viewer)
{
    if (viewer->bg_buffer && viewer->bg_width == viewer->width && viewer->bg_height == viewer->height)
    {
        return true;
    }

    if (viewer->bg_buffer)
    {
        wl_buffer_destroy(viewer->bg_buffer);
        viewer->bg_buffer = NULL;
    }
    if (viewer->bg_data)
    {
        munmap(viewer->bg_data, viewer->bg_size);
        viewer->bg_data = NULL;
    }

    size_t const stride = (size_t)viewer->width * 4;
    size_t const size = stride * (size_t)viewer->height;

    int const fd = create_shm_file(size);
    if (fd < 0)
    {
        fprintf(stderr, "failed to create shm file: %s\n", strerror(errno));
        return false;
    }

    void* data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
    {
        fprintf(stderr, "failed to map shm file: %s\n", strerror(errno));
        close(fd);
        return false;
    }

    struct wl_shm_pool* pool = wl_shm_create_pool(viewer->shm, fd, (int32_t)size);
    viewer->bg_buffer = wl_shm_pool_create_buffer(
        pool, 0, viewer->width, viewer->height, (int32_t)stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    viewer->bg_data = data;
    viewer->bg_size = size;
    viewer->bg_width = viewer->width;
    viewer->bg_height = viewer->height;
    return true;
}

static void fill_rect(struct viewer* viewer, int x, int y, int w, int h, uint32_t colour)
{
    for (int row = y; row < y + h; ++row)
    {
        if (row < 0 || row >= viewer->height)
        {
            continue;
        }
        for (int col = x; col < x + w; ++col)
        {
            if (col < 0 || col >= viewer->width)
            {
                continue;
            }
            viewer->bg_data[(size_t)row * (size_t)viewer->width + (size_t)col] = colour;
        }
    }
}

static void stroke_rect(struct viewer* viewer, struct rect r, uint32_t colour)
{
    fill_rect(viewer, r.x, r.y, r.w, 1, colour);
    fill_rect(viewer, r.x, r.y + r.h - 1, r.w, 1, colour);
    fill_rect(viewer, r.x, r.y, 1, r.h, colour);
    fill_rect(viewer, r.x + r.w - 1, r.y, 1, r.h, colour);
}

/* ----------------------------------------------------------------- tiles --- */

static void tile_request_buffer(struct tile* tile);

static void tile_destroy(struct tile* tile)
{
    if (!tile)
    {
        return;
    }

    wl_list_remove(&tile->link);

    if (tile->viewport)
    {
        wp_viewport_destroy(tile->viewport);
    }
    if (tile->subsurface)
    {
        wl_subsurface_destroy(tile->subsurface);
    }
    if (tile->surface)
    {
        wl_surface_destroy(tile->surface);
    }
    if (tile->buffer)
    {
        wl_buffer_destroy(tile->buffer);
    }
    if (tile->session)
    {
        ext_foreign_buffer_session_v1_destroy(tile->session);
    }
    if (tile->source)
    {
        ext_image_capture_source_v1_destroy(tile->source);
    }

    free(tile->label);
    free(tile);
}

static void session_size(
    void* data,
    struct ext_foreign_buffer_session_v1* session,
    uint32_t buffer_width,
    uint32_t buffer_height,
    uint32_t logical_width,
    uint32_t logical_height)
{
    (void)session;
    struct tile* tile = data;

    tile->buffer_width = buffer_width;
    tile->buffer_height = buffer_height;
    tile->logical_width = logical_width ? logical_width : buffer_width;
    tile->logical_height = logical_height ? logical_height : buffer_height;

    if (!tile->buffer_width || !tile->buffer_height)
    {
        return;
    }

    tile->have_size = true;
    tile->need_attach = true;

    printf(
        "[size]    %-40s buffer %ux%u logical %ux%u\n",
        tile->label,
        buffer_width,
        buffer_height,
        tile->logical_width,
        tile->logical_height);

    schedule_layout(tile->viewer);
}

static void session_buffer_invalidated(void* data, struct ext_foreign_buffer_session_v1* session)
{
    (void)session;
    struct tile* tile = data;

    printf("[reallocate] %s\n", tile->label);

    /*
     * Keep the previously submitted content on screen until a new size arrives; the old
     * wl_buffer must go away before get_buffer() is allowed again.
     */
    if (tile->buffer)
    {
        wl_buffer_destroy(tile->buffer);
        tile->buffer = NULL;
    }

    tile_request_buffer(tile);
}

static void session_stopped(void* data, struct ext_foreign_buffer_session_v1* session)
{
    (void)session;
    struct tile* tile = data;

    printf("[stopped] %s (freezing last content)\n", tile->label);

    tile->stopped = true;
    if (tile->session)
    {
        ext_foreign_buffer_session_v1_destroy(tile->session);
        tile->session = NULL;
    }
}

static struct ext_foreign_buffer_session_v1_listener const session_listener = {
    .size = session_size,
    .buffer_invalidated = session_buffer_invalidated,
    .stopped = session_stopped,
};

static void tile_request_buffer(struct tile* tile)
{
    if (!tile->session || tile->buffer)
    {
        return;
    }

    tile->buffer = ext_foreign_buffer_session_v1_get_buffer(tile->session);
}

/* Takes ownership of `source` and `label`. */
static struct tile* tile_create(struct viewer* viewer, struct ext_image_capture_source_v1* source, char* label)
{
    struct tile* tile = calloc(1, sizeof *tile);
    if (!tile)
    {
        fprintf(stderr, "out of memory\n");
        abort();
    }

    tile->viewer = viewer;
    tile->label = label;
    tile->source = source;

    tile->session = ext_foreign_buffer_manager_v1_get_session(viewer->foreign_buffer_manager, source);
    ext_foreign_buffer_session_v1_add_listener(tile->session, &session_listener, tile);

    tile->surface = wl_compositor_create_surface(viewer->compositor);
    tile->subsurface = wl_subcompositor_get_subsurface(viewer->subcompositor, tile->surface, viewer->surface);
    tile->viewport = wp_viewporter_get_viewport(viewer->viewporter, tile->surface);

    wl_subsurface_set_desync(tile->subsurface);
    wl_surface_set_input_region(tile->surface, viewer->empty_region);

    wl_list_insert(viewer->tiles.prev, &tile->link);

    tile_request_buffer(tile);

    printf("[capture] %s\n", tile->label);
    return tile;
}

/* ---------------------------------------------------------------- layout --- */

static void relayout(struct viewer* viewer)
{
    viewer->layout_dirty = false;

    if (!viewer->configured || !background_realloc(viewer))
    {
        return;
    }

    int const padding = 8;

    size_t count = 0;
    struct tile* tile;
    wl_list_for_each(tile, &viewer->tiles, link)
    {
        if (tile->have_size)
        {
            ++count;
        }
    }

    fill_rect(viewer, 0, 0, viewer->width, viewer->height, 0xff141414);

    if (count > 0)
    {
        int const cols = (int)ceil(sqrt((double)count));
        int const rows = (int)ceil((double)count / (double)cols);
        int const cell_w = viewer->width / cols;
        int const cell_h = viewer->height / rows;

        int index = 0;
        wl_list_for_each(tile, &viewer->tiles, link)
        {
            if (!tile->have_size)
            {
                continue;
            }

            int const col = index % cols;
            int const row = index / cols;
            ++index;

            int const avail_w = cell_w - 2 * padding;
            int const avail_h = cell_h - 2 * padding;
            if (avail_w <= 0 || avail_h <= 0)
            {
                tile->slot = (struct rect){0, 0, 0, 0};
                continue;
            }

            double const scale = fmin(
                (double)avail_w / (double)tile->logical_width, (double)avail_h / (double)tile->logical_height);

            int dst_w = (int)((double)tile->logical_width * scale);
            int dst_h = (int)((double)tile->logical_height * scale);
            if (dst_w < 1)
            {
                dst_w = 1;
            }
            if (dst_h < 1)
            {
                dst_h = 1;
            }

            tile->slot = (struct rect){
                .x = col * cell_w + (cell_w - dst_w) / 2,
                .y = row * cell_h + (cell_h - dst_h) / 2,
                .w = dst_w,
                .h = dst_h,
            };

            stroke_rect(
                viewer,
                (struct rect){tile->slot.x - 2, tile->slot.y - 2, tile->slot.w + 4, tile->slot.h + 4},
                tile->stopped ? 0xff803030 : 0xff3a6ea5);
        }
    }

    wl_surface_attach(viewer->surface, viewer->bg_buffer, 0, 0);
    wl_surface_damage_buffer(viewer->surface, 0, 0, INT32_MAX, INT32_MAX);

    wl_list_for_each(tile, &viewer->tiles, link)
    {
        if (!tile->have_size || tile->slot.w == 0)
        {
            continue;
        }

        wp_viewport_set_destination(tile->viewport, tile->slot.w, tile->slot.h);
        wl_subsurface_set_position(tile->subsurface, tile->slot.x, tile->slot.y);

        if (tile->need_attach && tile->buffer)
        {
            wl_surface_attach(tile->surface, tile->buffer, 0, 0);
            tile->need_attach = false;
        }

        wl_surface_damage_buffer(tile->surface, 0, 0, INT32_MAX, INT32_MAX);
        wl_surface_commit(tile->surface);
    }

    wl_surface_commit(viewer->surface);
}

/* ------------------------------------------------------------- xdg shell --- */

static void wm_base_ping(void* data, struct xdg_wm_base* wm_base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static struct xdg_wm_base_listener const wm_base_listener = {.ping = wm_base_ping};

static void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial)
{
    struct viewer* viewer = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    viewer->configured = true;
    relayout(viewer);
}

static struct xdg_surface_listener const xdg_surface_listener = {.configure = xdg_surface_configure};

static void xdg_toplevel_configure(
    void* data, struct xdg_toplevel* toplevel, int32_t width, int32_t height, struct wl_array* states)
{
    (void)toplevel;
    (void)states;
    struct viewer* viewer = data;

    if (width > 0 && height > 0 && (width != viewer->width || height != viewer->height))
    {
        viewer->width = width;
        viewer->height = height;
        schedule_layout(viewer);
    }
}

static void xdg_toplevel_close(void* data, struct xdg_toplevel* toplevel)
{
    (void)toplevel;
    struct viewer* viewer = data;
    viewer->running = false;
}

static struct xdg_toplevel_listener const xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

/* ------------------------------------------------------ foreign toplevels --- */

static void toplevel_destroy(struct toplevel* toplevel)
{
    wl_list_remove(&toplevel->link);
    tile_destroy(toplevel->tile);
    ext_foreign_toplevel_handle_v1_destroy(toplevel->handle);
    free(toplevel->title);
    free(toplevel->app_id);
    free(toplevel);
}

static void toplevel_maybe_start(struct toplevel* toplevel)
{
    struct viewer* viewer = toplevel->viewer;

    if (!viewer->ready || viewer->mode != MODE_TOPLEVELS || !toplevel->info_done || toplevel->started)
    {
        return;
    }

    toplevel->started = true;

    if (!viewer->include_self && toplevel->app_id && strcmp(toplevel->app_id, SELF_APP_ID) == 0)
    {
        printf("[skip]    our own toplevel (pass --include-self to capture it anyway)\n");
        return;
    }

    struct ext_image_capture_source_v1* source =
        ext_foreign_toplevel_image_capture_source_manager_v1_create_source(
            viewer->toplevel_source_manager, toplevel->handle);

    char label[256];
    snprintf(
        label,
        sizeof label,
        "%s [%s]",
        toplevel->title && *toplevel->title ? toplevel->title : "(untitled)",
        toplevel->app_id ? toplevel->app_id : "");

    toplevel->tile = tile_create(viewer, source, xstrdup(label));
}

static void handle_closed(void* data, struct ext_foreign_toplevel_handle_v1* handle)
{
    (void)handle;
    struct toplevel* toplevel = data;
    struct viewer* viewer = toplevel->viewer;

    printf("[closed]  %s\n", toplevel->title ? toplevel->title : "(untitled)");
    toplevel_destroy(toplevel);
    schedule_layout(viewer);
}

static void handle_done(void* data, struct ext_foreign_toplevel_handle_v1* handle)
{
    (void)handle;
    struct toplevel* toplevel = data;
    toplevel->info_done = true;
    toplevel_maybe_start(toplevel);
}

static void handle_title(void* data, struct ext_foreign_toplevel_handle_v1* handle, char const* title)
{
    (void)handle;
    struct toplevel* toplevel = data;
    free(toplevel->title);
    toplevel->title = xstrdup(title);
}

static void handle_app_id(void* data, struct ext_foreign_toplevel_handle_v1* handle, char const* app_id)
{
    (void)handle;
    struct toplevel* toplevel = data;
    free(toplevel->app_id);
    toplevel->app_id = xstrdup(app_id);
}

static void handle_identifier(void* data, struct ext_foreign_toplevel_handle_v1* handle, char const* identifier)
{
    (void)data;
    (void)handle;
    (void)identifier;
}

static struct ext_foreign_toplevel_handle_v1_listener const toplevel_handle_listener = {
    .closed = handle_closed,
    .done = handle_done,
    .title = handle_title,
    .app_id = handle_app_id,
    .identifier = handle_identifier,
};

static void toplevel_list_toplevel(
    void* data, struct ext_foreign_toplevel_list_v1* list, struct ext_foreign_toplevel_handle_v1* handle)
{
    (void)list;
    struct viewer* viewer = data;

    struct toplevel* toplevel = calloc(1, sizeof *toplevel);
    toplevel->viewer = viewer;
    toplevel->handle = handle;
    wl_list_insert(viewer->toplevels.prev, &toplevel->link);

    ext_foreign_toplevel_handle_v1_add_listener(handle, &toplevel_handle_listener, toplevel);
}

static void toplevel_list_finished(void* data, struct ext_foreign_toplevel_list_v1* list)
{
    (void)data;
    (void)list;
}

static struct ext_foreign_toplevel_list_v1_listener const toplevel_list_listener = {
    .toplevel = toplevel_list_toplevel,
    .finished = toplevel_list_finished,
};

/* --------------------------------------------------------------- outputs --- */

static void output_maybe_start(struct output* output)
{
    struct viewer* viewer = output->viewer;

    if (!viewer->ready || viewer->mode != MODE_OUTPUTS || !output->info_done || output->started)
    {
        return;
    }

    output->started = true;

    char const* name = output->name ? output->name : "";
    if (viewer->output_filter && strcmp(viewer->output_filter, name) != 0)
    {
        printf("[skip]    output %s (does not match --output %s)\n", name, viewer->output_filter);
        return;
    }

    struct ext_image_capture_source_v1* source =
        ext_output_image_capture_source_manager_v1_create_source(viewer->output_source_manager, output->output);

    char label[256];
    snprintf(label, sizeof label, "output: %s", name);

    output->tile = tile_create(viewer, source, xstrdup(label));
}

static void output_geometry(
    void* data,
    struct wl_output* wl_output,
    int32_t x,
    int32_t y,
    int32_t physical_width,
    int32_t physical_height,
    int32_t subpixel,
    char const* make,
    char const* model,
    int32_t transform)
{
    (void)wl_output;
    (void)x;
    (void)y;
    (void)physical_width;
    (void)physical_height;
    (void)subpixel;
    (void)transform;
    struct output* output = data;

    if (!output->name)
    {
        char buffer[256];
        snprintf(buffer, sizeof buffer, "%s %s", make ? make : "", model ? model : "");
        output->name = xstrdup(buffer);
    }
}

static void output_mode(
    void* data, struct wl_output* wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
    (void)data;
    (void)wl_output;
    (void)flags;
    (void)width;
    (void)height;
    (void)refresh;
}

static void output_done(void* data, struct wl_output* wl_output)
{
    (void)wl_output;
    struct output* output = data;
    output->info_done = true;
    output_maybe_start(output);
}

static void output_scale(void* data, struct wl_output* wl_output, int32_t factor)
{
    (void)data;
    (void)wl_output;
    (void)factor;
}

static void output_name(void* data, struct wl_output* wl_output, char const* name)
{
    (void)wl_output;
    struct output* output = data;
    free(output->name);
    output->name = xstrdup(name);
}

static void output_description(void* data, struct wl_output* wl_output, char const* description)
{
    (void)data;
    (void)wl_output;
    (void)description;
}

static struct wl_output_listener const output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

/* -------------------------------------------------------------- registry --- */

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static void registry_global(
    void* data, struct wl_registry* registry, uint32_t name, char const* interface, uint32_t version)
{
    struct viewer* viewer = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0)
    {
        viewer->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, min_u32(version, 4));
    }
    else if (strcmp(interface, wl_subcompositor_interface.name) == 0)
    {
        viewer->subcompositor = wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    }
    else if (strcmp(interface, wl_shm_interface.name) == 0)
    {
        viewer->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    }
    else if (strcmp(interface, wp_viewporter_interface.name) == 0)
    {
        viewer->viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    }
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
    {
        viewer->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, min_u32(version, 3));
        xdg_wm_base_add_listener(viewer->wm_base, &wm_base_listener, viewer);
    }
    else if (strcmp(interface, ext_foreign_buffer_manager_v1_interface.name) == 0)
    {
        viewer->foreign_buffer_manager =
            wl_registry_bind(registry, name, &ext_foreign_buffer_manager_v1_interface, 1);
    }
    else if (strcmp(interface, ext_output_image_capture_source_manager_v1_interface.name) == 0)
    {
        viewer->output_source_manager =
            wl_registry_bind(registry, name, &ext_output_image_capture_source_manager_v1_interface, 1);
    }
    else if (strcmp(interface, ext_foreign_toplevel_image_capture_source_manager_v1_interface.name) == 0)
    {
        viewer->toplevel_source_manager =
            wl_registry_bind(registry, name, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1);
    }
    else if (strcmp(interface, ext_foreign_toplevel_list_v1_interface.name) == 0)
    {
        if (viewer->mode != MODE_TOPLEVELS)
        {
            return;
        }

        viewer->toplevel_list = wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface, 1);
        /*
         * The listener has to be in place before the next dispatch, or the toplevel events the
         * server sends immediately on bind are discarded along with their new_id proxies.
         */
        ext_foreign_toplevel_list_v1_add_listener(viewer->toplevel_list, &toplevel_list_listener, viewer);
    }
    else if (strcmp(interface, wl_output_interface.name) == 0)
    {
        struct output* output = calloc(1, sizeof *output);
        output->viewer = viewer;
        output->global_name = name;
        output->output = wl_registry_bind(registry, name, &wl_output_interface, min_u32(version, 4));
        wl_list_insert(viewer->outputs.prev, &output->link);
        wl_output_add_listener(output->output, &output_listener, output);
    }
}

static void registry_global_remove(void* data, struct wl_registry* registry, uint32_t name)
{
    (void)registry;
    struct viewer* viewer = data;

    struct output* output;
    struct output* tmp;
    wl_list_for_each_safe(output, tmp, &viewer->outputs, link)
    {
        if (output->global_name != name)
        {
            continue;
        }

        wl_list_remove(&output->link);
        tile_destroy(output->tile);
        wl_output_destroy(output->output);
        free(output->name);
        free(output);
        schedule_layout(viewer);
    }
}

static struct wl_registry_listener const registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/* ------------------------------------------------------------------ main --- */

static void start_pending_sources(struct viewer* viewer)
{
    struct toplevel* toplevel;
    struct toplevel* toplevel_tmp;
    wl_list_for_each_safe(toplevel, toplevel_tmp, &viewer->toplevels, link)
    {
        toplevel_maybe_start(toplevel);
    }

    struct output* output;
    struct output* output_tmp;
    wl_list_for_each_safe(output, output_tmp, &viewer->outputs, link)
    {
        output_maybe_start(output);
    }
}

static void usage(char const* argv0)
{
    printf(
        "Usage: %s [OPTIONS]\n"
        "\n"
        "A test client for ext_foreign_buffer_v1. Tiles every capture source into one window.\n"
        "\n"
        "  --toplevels        capture every toplevel (default)\n"
        "  --outputs          capture every output instead\n"
        "  --output NAME      in --outputs mode, capture only the named output\n"
        "  --size WxH         initial window size (default 1280x720)\n"
        "  --include-self     do not skip this viewer's own toplevel\n"
        "                     (deliberately triggers the self-capture feedback loop)\n"
        "  --help             show this message\n",
        argv0);
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    struct viewer viewer = {
        .width = 1280,
        .height = 720,
        .mode = MODE_TOPLEVELS,
        .running = true,
    };

    wl_list_init(&viewer.tiles);
    wl_list_init(&viewer.toplevels);
    wl_list_init(&viewer.outputs);

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--outputs") == 0)
        {
            viewer.mode = MODE_OUTPUTS;
        }
        else if (strcmp(argv[i], "--toplevels") == 0)
        {
            viewer.mode = MODE_TOPLEVELS;
        }
        else if (strcmp(argv[i], "--include-self") == 0)
        {
            viewer.include_self = true;
        }
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc)
        {
            viewer.output_filter = argv[++i];
        }
        else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc)
        {
            if (sscanf(argv[++i], "%dx%d", &viewer.width, &viewer.height) != 2)
            {
                fprintf(stderr, "invalid --size, expected WxH\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        else
        {
            fprintf(stderr, "unrecognised argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    viewer.display = wl_display_connect(NULL);
    if (!viewer.display)
    {
        fprintf(stderr, "failed to connect to a Wayland display\n");
        return 1;
    }

    viewer.registry = wl_display_get_registry(viewer.display);
    wl_registry_add_listener(viewer.registry, &registry_listener, &viewer);
    wl_display_roundtrip(viewer.display);

    struct
    {
        void* ptr;
        char const* name;
    } const required[] = {
        {viewer.compositor, "wl_compositor"},
        {viewer.subcompositor, "wl_subcompositor"},
        {viewer.shm, "wl_shm"},
        {viewer.viewporter, "wp_viewporter"},
        {viewer.wm_base, "xdg_wm_base"},
        {viewer.foreign_buffer_manager, "ext_foreign_buffer_manager_v1"},
    };

    bool missing = false;
    for (size_t i = 0; i < sizeof required / sizeof required[0]; ++i)
    {
        if (!required[i].ptr)
        {
            fprintf(stderr, "compositor does not offer %s\n", required[i].name);
            missing = true;
        }
    }

    if (viewer.mode == MODE_OUTPUTS && !viewer.output_source_manager)
    {
        fprintf(stderr, "compositor does not offer ext_output_image_capture_source_manager_v1\n");
        missing = true;
    }

    if (viewer.mode == MODE_TOPLEVELS && (!viewer.toplevel_source_manager || !viewer.toplevel_list))
    {
        fprintf(
            stderr,
            "compositor does not offer ext_foreign_toplevel_image_capture_source_manager_v1 "
            "and/or ext_foreign_toplevel_list_v1\n");
        missing = true;
    }

    if (missing)
    {
        fprintf(
            stderr,
            "\nStart the server with, for example:\n"
            "  --add-wayland-extensions "
            "ext_foreign_buffer_manager_v1:ext_foreign_toplevel_list_v1:"
            "ext_foreign_toplevel_image_capture_source_manager_v1:"
            "ext_output_image_capture_source_manager_v1\n");
        return 1;
    }

    viewer.empty_region = wl_compositor_create_region(viewer.compositor);

    viewer.surface = wl_compositor_create_surface(viewer.compositor);
    viewer.xdg_surface = xdg_wm_base_get_xdg_surface(viewer.wm_base, viewer.surface);
    xdg_surface_add_listener(viewer.xdg_surface, &xdg_surface_listener, &viewer);
    viewer.xdg_toplevel = xdg_surface_get_toplevel(viewer.xdg_surface);
    xdg_toplevel_add_listener(viewer.xdg_toplevel, &xdg_toplevel_listener, &viewer);
    xdg_toplevel_set_title(viewer.xdg_toplevel, "Foreign buffer viewer");
    xdg_toplevel_set_app_id(viewer.xdg_toplevel, SELF_APP_ID);
    wl_surface_commit(viewer.surface);

    wl_display_roundtrip(viewer.display);

    viewer.ready = true;
    start_pending_sources(&viewer);

    while (viewer.running)
    {
        if (wl_display_dispatch(viewer.display) < 0)
        {
            break;
        }

        if (viewer.layout_dirty)
        {
            relayout(&viewer);
        }

        wl_display_flush(viewer.display);
    }

    return 0;
}
