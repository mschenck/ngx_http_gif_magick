/*
 * ngx_http_gif_magick
 *
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <wand/MagickWand.h>

#define MAX_IMAGE_SIZE 20971520  /* 20 MB */

typedef struct {
  ngx_uint_t  width;
  ngx_uint_t  height;
} ngx_http_gif_magick_loc_conf_t;


static char *     ngx_http_gif_magick ( ngx_conf_t *cf, ngx_command_t *cmd, void *conf );
static void *     ngx_http_gif_magick_create_loc_conf ( ngx_conf_t *cf ); 
static char *     ngx_http_gif_magick_merge_loc_conf ( ngx_conf_t *cf, void *parent, void *child );
static ngx_int_t  ngx_http_gif_magick_handler ( ngx_http_request_t *request );

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
  ngx_http_gif_magick_create_loc_conf, // creating the location conf
  ngx_http_gif_magick_merge_loc_conf, // merging it with the server conf
};

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
};

static char *
ngx_http_gif_magick( ngx_conf_t *cf, ngx_command_t *cmd, void *conf ) {

  return NGX_CONF_OK;
}

static void *
ngx_http_gif_magick_create_loc_conf( ngx_conf_t *cf ) {
  ngx_http_gif_magick_loc_conf_t *conf;
  
  conf = ngx_pcalloc( cf->pool, sizeof( ngx_http_gif_magick_loc_conf_t ) );
  if ( conf == NULL ) {
    return NGX_CONF_ERROR;
  }

  conf->width = NGX_CONF_UNSET_UINT;
  conf->height = NGX_CONF_UNSET_UINT;
  return conf;
}

static char *
ngx_http_gif_magick_merge_loc_conf( ngx_conf_t *cf, void *parent, void *child ) {
  ngx_http_gif_magick_loc_conf_t *prev = parent;
  ngx_http_gif_magick_loc_conf_t *conf = child;

  ngx_conf_merge_uint_value( conf->width, prev->width, 400 );
  ngx_conf_merge_uint_value( conf->height, prev->height, 1024 );

  return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_gif_magick_handler  ( ngx_http_request_t *request ) {
  ngx_http_gif_magick_loc_conf_t  *gif_magick_conf;
  ngx_buf_t                       *buffer;
  ngx_chain_t                     out_chain;

  // Load gif_magick config
  gif_magick_conf = ngx_http_get_module_loc_conf( request, ngx_http_gif_magick_module );

  // Headers
  request->headers_out.status = NGX_HTTP_OK;
  ngx_http_send_header( request );

  // Body
  buffer = ngx_pcalloc( request->pool, sizeof( ngx_buf_t ) );
  if ( buffer == NULL ) {
    ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "Failed to allocate response buffer.");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }

  out_chain.buf = buffer;
  out_chain.next = NULL;

  return ngx_http_output_filter( request, &out_chain );
}

