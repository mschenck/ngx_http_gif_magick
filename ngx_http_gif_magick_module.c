/*
 * ngx_http_gif_magick
 *
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <wand/MagickWand.h>

#define GIF_MAGICK_ENABLED                (ngx_flag_t)1
#define GIF_MAGICK_DISABLED               (ngx_flag_t)0
#define GIF_MAGICK_DEFAULT_BUFFER_SIZE    (size_t)20971520  /* 20 MB TODO:configurable */

typedef struct {
  ngx_flag_t    enabled;        // (default: DISABLED) enable plugin on a given location
  size_t        buffer_size;    // This determines the large image that will be processed
  size_t        width;          // (default: 400) Target resize width
  size_t        height;         // (default: 1024) Target resize height (a sloppy safety net, retains aspect ratio)
} ngx_http_gif_magick_loc_conf_t;

typedef struct {
  u_char        *gif_data;
  u_char        *gif_last;
  size_t        gif_size;
  size_t        buffer_size;
  size_t        width;          // (default: 400) Target resize width
  size_t        height;         // (default: 1024) Target resize height (a sloppy safety net, retains aspect ratio)
} ngx_http_gif_magick_ctx_t;

static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;

static void *     ngx_http_gif_magick_create_loc_conf( ngx_conf_t *cf ); 
static char *     ngx_http_gif_magick_merge_loc_conf( ngx_conf_t *cf, void *parent, void *child );
static char *     ngx_http_gif_magick_enable_setting( ngx_conf_t *cf, ngx_command_t *cmd, void *conf );

static ngx_int_t  ngx_http_gif_magick_read_image( ngx_http_request_t *request, ngx_chain_t *in_chain );
static ngx_int_t  ngx_http_gif_magick_resize_image( ngx_http_request_t *request, ngx_chain_t *in_chain );
static ngx_int_t  ngx_http_gif_magick_send_image( ngx_http_request_t *request, ngx_chain_t *in_chain );
static ngx_int_t  ngx_http_gif_magick_header_filter( ngx_http_request_t *request );
static ngx_int_t  ngx_http_gif_magick_body_filter( ngx_http_request_t *request, ngx_chain_t *in_chain );
static ngx_int_t  ngx_http_gif_magick_init( ngx_conf_t *cf );

static ngx_command_t ngx_http_gif_magick_commands[] = {
  {
    ngx_string( "gif_magick" ), // 'gif_magick_resize' would be preferable, see if linked to conf_t
    NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
    ngx_http_gif_magick_enable_setting,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL 
  },
  {
    ngx_string( "gif_magick_buffer" ), // 'gif_magick_resize' would be preferable, see if linked to conf_t
    NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_size_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof( ngx_http_gif_magick_loc_conf_t, buffer_size ),
    NULL 
  },
  {
    ngx_string( "gif_magick_width" ), // 'gif_magick_resize' would be preferable, see if linked to conf_t
    NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof( ngx_http_gif_magick_loc_conf_t, width ),
    NULL 
  },
  {
    ngx_string( "gif_magick_height" ), // 'gif_magick_resize' would be preferable, see if linked to conf_t
    NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof( ngx_http_gif_magick_loc_conf_t, height ),
    NULL 
  },
  ngx_null_command
};

static ngx_http_module_t ngx_http_gif_magick_module_ctx = {
  NULL, // preconfiguration
  ngx_http_gif_magick_init, // postconfiguration
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

  conf->enabled      = NGX_CONF_UNSET;
  conf->width        = NGX_CONF_UNSET_SIZE;
  conf->height       = NGX_CONF_UNSET_SIZE;
  conf->buffer_size  = NGX_CONF_UNSET_SIZE;

  return conf;
}

static char *
ngx_http_gif_magick_merge_loc_conf( ngx_conf_t *cf, void *parent, void *child )
{
  ngx_http_gif_magick_loc_conf_t *prev = parent;
  ngx_http_gif_magick_loc_conf_t *conf = child;

  ngx_conf_merge_off_value( conf->enabled, prev->enabled, GIF_MAGICK_DISABLED );
  ngx_conf_merge_size_value( conf->buffer_size, prev->buffer_size, GIF_MAGICK_DEFAULT_BUFFER_SIZE );
  ngx_conf_merge_size_value( conf->width, prev->width, 400 );
  ngx_conf_merge_size_value( conf->height, prev->height, 1024 );

  return NGX_CONF_OK;
}

static char *
ngx_http_gif_magick_enable_setting( ngx_conf_t *cf, ngx_command_t *cmd, void *conf )
{
  return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_gif_magick_header_filter( ngx_http_request_t *request )
{
  //ngx_http_gif_magick_ctx_t   *ctx;
  //ctx = ngx_http_get_module_ctx( request, ngx_http_gif_magick_module );

  // TODO: add logic to verify the received body and headers

      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "starting the header_filter.");

  return NGX_OK;
}
// NOTE: The creation/clean-op of a MagickWand per run is intentional (and fairly cheap, all-in-all)
static ngx_int_t
ngx_http_gif_magick_resize_image( ngx_http_request_t *request, ngx_chain_t *in_chain )
{
  ngx_http_gif_magick_ctx_t       *ctx;
  MagickWand                      *magick_wand;
  MagickSizeType                  magick_length;

  ctx = ngx_http_get_module_ctx( request, ngx_http_gif_magick_module );
  if ( ctx == NULL ) {
    ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "Failed to retrieve ctx for resizing" );
    return NGX_ERROR;
  }

      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "INITIALIZING WAND.");


  // Initialize this Wand
  MagickWandGenesis();
  magick_wand = NewMagickWand();

      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "LOADING IMAGE INTO WAND.");

  // Load original image into Wand
  if ( MagickReadImageBlob( magick_wand, (void*)ctx->gif_data, ctx->gif_size) == MagickFalse ) {
    ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "Magick fed an invalid image blob");
    return NGX_ERROR;
  }

      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "RESIZE LOADED IMAGE IN WAND.");

  // Resize all frames
  MagickCoalesceImages( magick_wand );

  // Resize current 'image' (frame)
  MagickSetFirstIterator( magick_wand );
  do {
    MagickAdaptiveResizeImage( magick_wand, ctx->width, ctx->height );
  } while ( MagickNextImage( magick_wand ) != MagickFalse );

  MagickStripImage( magick_wand );
  MagickEqualizeImage( magick_wand );

  // Fix any optimizations we broke in coalesce
  MagickOptimizeImageLayers( magick_wand );

  // Update image content and size
  MagickGetImageLength( magick_wand, &magick_length );
  ctx->gif_size = (size_t)magick_length;
  ctx->gif_data = MagickGetImagesBlob( magick_wand, &ctx->gif_size );

      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "UPDATING IMAGE SIZE [ %uz ].", ctx->gif_size);


  // Magick time over ... cleaning up
  magick_wand = DestroyMagickWand( magick_wand );
  MagickWandTerminus();

  return NGX_OK;
}

static ngx_int_t
ngx_http_gif_magick_read_image( ngx_http_request_t *request, ngx_chain_t *in_chain )
{
  ngx_http_gif_magick_ctx_t       *ctx;
  u_char                          *gif_pos;
  ngx_buf_t                       *buffer;
  ngx_chain_t                     *chain_link;
  size_t                          chunk_size, remaining;

  // Retrieve (or allocate if missing) context
  ctx = ngx_http_get_module_ctx( request, ngx_http_gif_magick_module );


      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "Allocating %uz of image data" , ctx->buffer_size );

  if ( ctx->gif_data == NULL ) {
    ctx->gif_data = ngx_pcalloc( request->pool, ctx->buffer_size );
    if ( ctx->gif_data == NULL ) {
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "Failed to allocate response buffer.");
      return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    ctx->gif_last = ctx->gif_data;
  }

  gif_pos = ctx->gif_last;
  chunk_size = 0;

  for ( chain_link = in_chain; chain_link != NULL; chain_link = chain_link->next) {

    buffer = chain_link->buf;
    chunk_size = buffer->last - buffer->pos;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "buffered %uzB of image data" , chunk_size );

      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "buffered %uzB of image data" , chunk_size * sizeof( u_char ) );

    remaining = ctx->gif_data + ctx->buffer_size - gif_pos;
    if ( remaining < chunk_size ) chunk_size = remaining;

      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "'remaining' = %uz and 'chunk_size' now = %uz" , remaining, chunk_size );


    gif_pos = ngx_cpymem( gif_pos, buffer->pos, chunk_size );
    buffer->pos += chunk_size;
    ctx->gif_size = ctx->gif_last - ctx->gif_data + chunk_size;

      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "SIZE OF 'gif_data' NOW %uzB" , ctx->gif_size );

  }

      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "finished got reading.");

  ctx->gif_last = gif_pos;
  ctx->gif_size = ctx->gif_last - ctx->gif_data + chunk_size;

  //FIXME
  ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "FINISHED READ (%uzB) OK.", ctx->gif_size );


  return NGX_OK;

}

static ngx_int_t
ngx_http_gif_magick_send_image( ngx_http_request_t *request, ngx_chain_t *in_chain )
{
  ngx_http_gif_magick_ctx_t   *ctx;
  ngx_buf_t                   *buffer;
  ngx_chain_t                 out_chain;
  ngx_str_t                   content_type = ngx_string("image/gif"); 

  ctx = ngx_http_get_module_ctx( request, ngx_http_gif_magick_module );

  // Set response body
  buffer = ngx_pcalloc( request->pool, sizeof( ngx_buf_t ) );
  if ( buffer == NULL ) return NGX_ERROR;

  buffer->pos = ctx->gif_data;
  buffer->last = ctx->gif_last;
  buffer->memory = 1;
  buffer->last_buf = 1;


      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "the sizeof gif_data is (%d)", sizeof( ctx->gif_data ) );


  // Add new link to chain
  out_chain.buf = buffer;
  out_chain.next = NULL;

  // Set response headers
  request->headers_out.content_length_n = ctx->gif_size;

  if ( request->headers_out.content_length ) {
    request->headers_out.content_length->hash = 0;
  }
  request->headers_out.content_length = NULL;

  // Headers
  //content_type = ngx_string("image/gif"); 
  request->headers_out.content_type_len = content_type.len;
  request->headers_out.content_type = content_type;
  request->headers_out.content_type_lowcase = NULL;

  request->headers_out.status = NGX_HTTP_OK;

  return ngx_http_next_body_filter( request, &out_chain );
}

static ngx_int_t
ngx_http_gif_magick_body_filter  ( ngx_http_request_t *request, ngx_chain_t *in_chain )
{
  ngx_int_t                       result;
  ngx_http_gif_magick_ctx_t       *ctx;
  ngx_http_gif_magick_loc_conf_t  *gif_magick_conf;

      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "starting the body_filter.");

  // Return quickly if not needed
  //if ( request->header_only || in_chain == NULL ) return ngx_http_next_body_filter( request, in_chain );

  // Fetch config
  gif_magick_conf = ngx_http_get_module_loc_conf( request, ngx_http_gif_magick_module );

  // Instantiate context
  ctx = ngx_http_get_module_ctx( request, ngx_http_gif_magick_module );
  if ( ctx == NULL ) {
    ctx = ngx_pcalloc( request->pool, sizeof( ngx_http_gif_magick_ctx_t ) );
    if ( ctx == NULL ) {
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "Failed to allocate new ctx" );
      return NGX_ERROR;
    }
    ngx_http_set_ctx( request, ctx, ngx_http_gif_magick_module );
  }
  
  ctx->buffer_size  = gif_magick_conf->buffer_size;
  ctx->width        = gif_magick_conf->width;
  ctx->height       = gif_magick_conf->height;

      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "starting to read image.");

  // Read image into ctx
  result = ngx_http_gif_magick_read_image( request, in_chain );
  if ( result == NGX_ERROR ) {
    ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "Failed to read image file.");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }

      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "starting to resize image.");

  // Resize image (from ctx)
  ngx_http_gif_magick_resize_image( request, in_chain );

      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "starting to send image.");

  // Send resized image (from ctx)
  return ngx_http_gif_magick_send_image( request, in_chain );
}

static ngx_int_t
ngx_http_gif_magick_init( ngx_conf_t *cf )
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_gif_magick_header_filter;

    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_gif_magick_body_filter;

    return NGX_OK;
}
