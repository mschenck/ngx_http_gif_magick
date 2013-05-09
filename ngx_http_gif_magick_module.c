/*
 * ngx_http_gif_magick
 *
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <wand/MagickWand.h>

#define MAX_IMAGE_SIZE 20971520  /* 20 MB TODO:configurable */

typedef struct {
  ngx_uint_t  width;
  ngx_uint_t  height;
} ngx_http_gif_magick_loc_conf_t;

static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;

static void *     ngx_http_gif_magick_create_loc_conf( ngx_conf_t *cf ); 
static char *     ngx_http_gif_magick_merge_loc_conf( ngx_conf_t *cf, void *parent, void *child );
static char *     ngx_http_gif_magick( ngx_conf_t *cf, ngx_command_t *cmd, void *conf );

static ngx_int_t  ngx_http_gif_magick_header_filter( ngx_http_request_t *request );
static ngx_int_t  ngx_http_gif_magick_body_filter( ngx_http_request_t *request, ngx_chain_t *in_chain );
static ngx_int_t  ngx_http_image_filter_init( ngx_conf_t *cf );

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
  ngx_http_image_filter_init, // postconfiguration
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

static void *
ngx_http_gif_magick_create_loc_conf( ngx_conf_t *cf )
{
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
ngx_http_gif_magick_merge_loc_conf( ngx_conf_t *cf, void *parent, void *child )
{
  ngx_http_gif_magick_loc_conf_t *prev = parent;
  ngx_http_gif_magick_loc_conf_t *conf = child;

  ngx_conf_merge_uint_value( conf->width, prev->width, 400 );
  ngx_conf_merge_uint_value( conf->height, prev->height, 1024 );

  return NGX_CONF_OK;
}

static char *
ngx_http_gif_magick( ngx_conf_t *cf, ngx_command_t *cmd, void *conf )
{
  ngx_http_gif_magick_loc_conf_t  *gif_magick_conf = conf;

  // TODO: This is incredibly unlikely - but need to kill a warning for now
  if ( gif_magick_conf == NULL ) return NGX_CONF_ERROR;

  return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_gif_magick_header_filter( ngx_http_request_t *request )
{
  // TODO: add logic to verify the received body and headers
  return ngx_http_next_header_filter( request );
}

// NOTE: The creation/clean-op of a MagickWand per run is intentional (and fairly cheap, all-in-all)
static ngx_int_t
ngx_http_gif_magick_body_filter  ( ngx_http_request_t *request, ngx_chain_t *in_chain )
{
  ngx_http_gif_magick_loc_conf_t  *gif_magick_conf;
  ngx_chain_t                     *chain_link;
  short                           last_found = 0;
  MagickWand                      *magick_wand;
  void                            *gif_data, *gif_pos;
  ssize_t                         chunk_size, gif_size;

  // Return quickly if not needed
  if ( request->header_only || in_chain == NULL ) {
    return ngx_http_next_body_filter( request, in_chain );
  }

  // Load gif_magick config
  gif_magick_conf = ngx_http_get_module_loc_conf( request, ngx_http_gif_magick_module );

  // Headers
  request->headers_out.status = NGX_HTTP_OK;
  ngx_http_send_header( request );

  // Body
  gif_data = ngx_pcalloc( request->pool, MAX_IMAGE_SIZE );
  if ( gif_data == NULL ) {
    ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "Failed to allocate response buffer.");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }

  gif_pos = gif_data;
  gif_size = 0;
  for ( chain_link = in_chain; chain_link->next == NULL ; chain_link = chain_link->next) {
    chunk_size = chain_link->buf->last - chain_link->buf->pos;
    gif_pos = ngx_cpymem(gif_pos, chain_link->buf->pos, chunk_size );
    gif_size += chunk_size;
    if ( chain_link->buf->last_buf ) last_found = 1; // final buffer found
  }

  // Move on to the next filter if there is no final buffer
  if ( !last_found || gif_size == 0 ) return ngx_http_next_body_filter( request, in_chain );

  // Initialize this Wand
  MagickWandGenesis();
  magick_wand = NewMagickWand();

  // Load original image into Wand
  if ( MagickReadImageBlob( magick_wand, gif_data, gif_size ) == MagickFalse ) {
    ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "Magick fed an invalid image blob");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }

  // Resize all frames
  MagickCoalesceImages( magick_wand );

  // Resize current 'image' (frame)
  MagickSetFirstIterator( magick_wand );
  do {
    MagickAdaptiveResizeImage( magick_wand, gif_magick_conf->width, gif_magick_conf->height );
  } while ( MagickNextImage( magick_wand ) != MagickFalse );


  MagickStripImage( magick_wand );
  MagickEqualizeImage( magick_wand );

  // Fix any optimizations we broke in coalesce
  MagickOptimizeImageLayers( magick_wand );

  // Magick time over ... cleaning up
  magick_wand = DestroyMagickWand( magick_wand );
  MagickWandTerminus();

  return ngx_http_next_body_filter( request, in_chain );
}

static ngx_int_t
ngx_http_image_filter_init( ngx_conf_t *cf )
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_gif_magick_header_filter;

    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_gif_magick_body_filter;

    return NGX_OK;
}
