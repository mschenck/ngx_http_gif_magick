/*
 * ngx_http_gif_magick
 *
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
  ngx_uint_t  methods;
  ngx_flag_t  create_full_put_path;
  ng
} ngx_http_gif_magick_loc_conf_t;


static char * ngx_http_gif_magick(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

  return NGX_OK;
}


static ngx_command_t  ngx_http_gif_magick_commands[] = {
  {
    ngx_string("gif_magick_resize"),
    NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
    ngx_http_gif_magick,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL 
  },
  ngx_null_command
}


