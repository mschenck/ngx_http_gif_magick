/*
 * ngx_http_gif_magick
 *
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
  ngx_uint_t  width;
  ngx_uint_t  height;
} ngx_http_gif_magick_loc_conf_t;

static void * ngx_http_gif_magick_loc_conf( ngx_conf_t *cf ); 
static char * ngx_http_gif_magick( ngx_conf_t *cf, ngx_command_t *cmd, void *conf );

static ngx_command_t ngx_http_gif_magick_commands[] = {
  {
    ngx_string( "gif_magick" ), // 'gif_magick_resize' would be preferable, see if linked to conf_t
    NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
    ngx_http_gif_magick,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL 
  },
  ngx_null_command
};

static ngx_http_module_t ngx_http_gif_magick_module_ctx = {
  NULL, // preconfiguration
  NULL, // postconfiguration
  NULL, // creating the main conf ( i.e., do a malloc and set defaults )
  NULL, // initializing the main conf ( override the defaults with what's in nginx.conf )
  NULL, // creating the server conf
  NULL, // merging it with the main conf
  ngx_http_gif_magick_loc_conf, // creating the location conf
  NULL, // merging it with the server conf
};

static void * ngx_http_gif_magick_loc_conf( ngx_conf_t *cf ) {
  ngx_http_gif_magick_loc_conf_t *conf;
  
  conf = ngx_pcalloc( cf->pool, sizeof( ngx_http_gif_magick_loc_conf_t ) );
  if ( conf == NULL ) {
    return NGX_CONF_ERROR;
  }

  conf->width = NGX_CONF_UNSET_UINT;
  conf->height = NGX_CONF_UNSET_UINT;
  return conf;
}

ngx_module_t ngx_http_gif_magick_module = {
  NGX_MODULE_V1,
  &ngx_http_gif_magick_module_ctx,
  ngx_http_gif_magick_commands,
  NGX_HTTP_MODULE,
  NULL, // init master
  NULL, // init module
  NULL, // init process
  NULL, // init thread
  NULL, // exit thread
  NULL, // exit process
  NULL, // exit master
  NGX_MODULE_V1_PADDING
}

static char * ngx_http_gif_magick( ngx_conf_t *cf, ngx_command_t *cmd, void *conf ) {

  return NGX_OK;
}

