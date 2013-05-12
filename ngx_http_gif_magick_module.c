/*
 * ngx_http_gif_magick
 *
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <wand/MagickWand.h>

#define GIF_MAGICK_DISABLED     0
#define GIF_MAGICK_ENABLED      1
#define GIF_MAGICK_READ         2
#define GIF_MAGICK_RESIZE       3
#define GIF_MAGICK_SEND         4
#define GIF_MAGICK_DONE         5

#define GIF_MAGICK_DEFAULT_BUFFER_SIZE    (size_t)20971520  /* 20 MB TODO:configurable */

typedef struct {
  ngx_flag_t    enabled;        // (default: DISABLED) enable plugin on a given location
  size_t        buffer_size;    // This determines the large image that will be processed
  ngx_int_t     width;          // Target resize width
  ngx_int_t     height;         // Target resize height (a sloppy safety net, retains aspect ratio)
} ngx_http_gif_magick_loc_conf_t;

typedef struct {
  u_char        *gif_data;
  u_char        *gif_last;
  size_t        gif_size;
  size_t        buffer_size;
  ngx_int_t     width;          // Target resize width
  ngx_int_t     height;         // Target resize height (a sloppy safety net, retains aspect ratio)
  ngx_uint_t    status;         // Processing status
} ngx_http_gif_magick_ctx_t;

static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;

static void *     ngx_http_gif_magick_create_loc_conf( ngx_conf_t *cf ); 
static char *     ngx_http_gif_magick_merge_loc_conf( ngx_conf_t *cf, void *parent, void *child );

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
    ngx_conf_set_flag_slot,
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
  conf->width        = NGX_CONF_UNSET;
  conf->height       = NGX_CONF_UNSET;
  conf->buffer_size  = NGX_CONF_UNSET_SIZE;

  return conf;
}

static char *
ngx_http_gif_magick_merge_loc_conf( ngx_conf_t *cf, void *parent, void *child )
{
  ngx_http_gif_magick_loc_conf_t *prev = parent;
  ngx_http_gif_magick_loc_conf_t *conf = child;

  ngx_conf_merge_value( conf->enabled, prev->enabled, GIF_MAGICK_DISABLED );
  ngx_conf_merge_size_value( conf->buffer_size, prev->buffer_size, GIF_MAGICK_DEFAULT_BUFFER_SIZE );
  ngx_conf_merge_value( conf->width, prev->width, NGX_CONF_UNSET);
  ngx_conf_merge_value( conf->height, prev->height, NGX_CONF_UNSET );

  return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_gif_magick_header_filter( ngx_http_request_t *request )
{
  ngx_http_gif_magick_ctx_t       *ctx;
  ngx_http_gif_magick_loc_conf_t  *gif_magick_conf;

  ngx_log_debug0( NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "GIF_MAGICK header_filter.");

  // Return quickly if not needed

  if ( request->headers_out.status == NGX_HTTP_NOT_MODIFIED )                     // Not Modified
    return ngx_http_next_header_filter( request );



  ctx = ngx_http_get_module_ctx( request, ngx_http_gif_magick_module );
  if ( ctx == NULL ) {
    ctx = ngx_pcalloc( request->pool, sizeof( ngx_http_gif_magick_ctx_t ) );      // Instantiate context
    if ( ctx == NULL ) {
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "Failed to allocate new ctx" );
      return NGX_ERROR;
    }
    ngx_http_set_ctx( request, ctx, ngx_http_gif_magick_module );
  }

  gif_magick_conf = ngx_http_get_module_loc_conf( request, ngx_http_gif_magick_module );
  if ( gif_magick_conf->enabled == GIF_MAGICK_DISABLED ) {                        // gif_magick disabled
    ctx->status = GIF_MAGICK_DISABLED;
    return ngx_http_next_header_filter( request );
  }

  // Prepare for resizing
 
  ngx_log_debug0( NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "gif_magick preparing for resizing.");

  ctx->status       = GIF_MAGICK_ENABLED; 
  ctx->buffer_size  = gif_magick_conf->buffer_size;
  ctx->width        = gif_magick_conf->width;
  ctx->height       = gif_magick_conf->height;

  // TODO: add logic to verify the received body and headers
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

  // Initialize this Wand
  ngx_log_debug0( NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "gif_magick initializing wand and loading image data.");
  MagickWandGenesis();
  magick_wand = NewMagickWand();

  // Load original image into Wand
  if ( MagickReadImageBlob( magick_wand, (void*)ctx->gif_data, ctx->buffer_size) == MagickFalse ) {
    ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "Magick fed an invalid image blob");
    return NGX_ERROR;
  }

  // Resize all frames
  MagickCoalesceImages( magick_wand );

  MagickSetFirstIterator( magick_wand );
  do {
    MagickAdaptiveResizeImage( magick_wand, ctx->width, ctx->height );
  } while ( MagickNextImage( magick_wand ) != MagickFalse );

  MagickStripImage( magick_wand );
  MagickEqualizeImage( magick_wand );

  MagickOptimizeImageLayers( magick_wand );           // Fix any optimizations we broke in coalesce

  // Update image content and size
  MagickGetImageLength( magick_wand, &magick_length );
  ctx->gif_size = (size_t)magick_length;
  ctx->gif_data = MagickGetImagesBlob( magick_wand, &ctx->gif_size );

  ngx_log_debug1( NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "gif_magic updated context image size to %uz B from wand.", ctx->gif_size);

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
      
  if ( ctx->gif_data == NULL ) {
    ngx_log_debug1( NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "gif_magick allocating %uz B for image data" , ctx->buffer_size );
    ctx->gif_data = ngx_palloc( request->pool, ctx->buffer_size );
    if ( ctx->gif_data == NULL ) {
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "Failed to allocate response buffer.");
      return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    ctx->gif_size = 0;
    ctx->gif_last = ctx->gif_data;
  }

  gif_pos = ctx->gif_last;
  for ( chain_link = in_chain; chain_link; chain_link = chain_link->next) {

    buffer      = chain_link->buf;
    chunk_size  = buffer->last - buffer->pos;

    remaining = ctx->gif_data + ctx->buffer_size - gif_pos;
    if ( remaining < chunk_size ) chunk_size = remaining;
      
    ngx_log_debug2( NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "gif_magick context image buffer: Remaining: %uz B - chunk size: %uz B" , remaining, chunk_size );

    gif_pos         = ngx_cpymem( gif_pos, buffer->pos, chunk_size );
    buffer->pos    += chunk_size;
    ctx->gif_size  += chunk_size * sizeof( u_char );

    if ( buffer->last_buf ) {
      ctx->gif_last = gif_pos;
      ngx_log_debug1( NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "gif_magick read image ( %uz B ) successful.", ctx->gif_size ); 
      return NGX_OK;
    }

  }

  ctx->gif_last = gif_pos;
  request->connection->buffered |= 0x08;

  return NGX_AGAIN;
}

static ngx_int_t
ngx_http_gif_magick_send_image( ngx_http_request_t *request, ngx_chain_t *in_chain )
{
  ngx_http_gif_magick_ctx_t   *ctx;
  ngx_buf_t                   *buffer;
  ngx_chain_t                 out_chain;
  ngx_int_t                   result;
  ngx_str_t                   content_type = ngx_string("image/gif"); 

  ctx = ngx_http_get_module_ctx( request, ngx_http_gif_magick_module );
  if ( ctx == NULL ) ngx_http_next_body_filter( request, in_chain );

  // Set response body
  buffer = ngx_pcalloc( request->pool, sizeof( ngx_buf_t ) );
  if ( buffer == NULL ) return NGX_ERROR;

  buffer->pos       = ctx->gif_data;
  buffer->last      = ctx->gif_data + ctx->gif_size;
  buffer->memory    = 1;
  buffer->last_buf  = 1;

  out_chain.buf = buffer;
  out_chain.next = NULL;

  // Set response headers
  request->headers_out.content_length_n = ctx->gif_size;
  ngx_log_debug1( NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "gif_magick response content-lenght: %d", ctx->gif_size );

  if ( request->headers_out.content_length ) request->headers_out.content_length->hash = 0;
  request->headers_out.content_length = NULL;

  request->headers_out.content_type_len = content_type.len;
  request->headers_out.content_type = content_type;
  request->headers_out.content_type_lowcase = NULL;
  request->headers_out.status = NGX_HTTP_OK;

  result = ngx_http_next_header_filter( request );
  if ( result == NGX_ERROR || request->header_only ) return NGX_ERROR;

  ctx->status = GIF_MAGICK_DONE;

  return ngx_http_next_body_filter( request, &out_chain );
}

static ngx_int_t
ngx_http_gif_magick_body_filter  ( ngx_http_request_t *request, ngx_chain_t *in_chain )
{
  ngx_http_gif_magick_ctx_t       *ctx;
  ngx_int_t                       result;

  ngx_log_debug0( NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "GIF_MAGICK body_filter.");

  // Return quickly if not needed
  if ( in_chain == NULL ) return ngx_http_next_body_filter( request, in_chain );

  ctx = ngx_http_get_module_ctx( request, ngx_http_gif_magick_module );
  if ( ctx == NULL ) ngx_http_next_body_filter( request, in_chain );

  switch( ctx->status ) {

    case GIF_MAGICK_DISABLED:

      return ngx_http_next_body_filter( request, in_chain );

    case GIF_MAGICK_ENABLED:

      ngx_log_debug0( NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "gif_magick starting to read image.");
      ctx->status = GIF_MAGICK_READ;

    case GIF_MAGICK_READ:

      ngx_log_debug0( NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "gif_magick reading image.");
      result = ngx_http_gif_magick_read_image( request, in_chain );
      if ( result == NGX_ERROR ) {
        ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "Failed to read image file.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
      }

      if ( result == NGX_ERROR ) ctx->status = GIF_MAGICK_RESIZE;

    case GIF_MAGICK_RESIZE:

      ngx_log_debug0( NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "gif_magick resizing.");
      result = ngx_http_gif_magick_resize_image( request, in_chain );
      if ( result == NGX_ERROR ) {
        ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "Failed to resize image file.");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
      }
      ctx->status = GIF_MAGICK_SEND;

    case GIF_MAGICK_SEND:

      ngx_log_debug0( NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "gif_magick sending response.");
      return ngx_http_gif_magick_send_image( request, in_chain );

    case GIF_MAGICK_DONE:

      ngx_log_debug0( NGX_LOG_DEBUG_HTTP, request->connection->log, 0, "gif_magick complete.");
      return ngx_http_next_body_filter( request, in_chain );
   
    default:

      //FIXME
      ngx_log_error( NGX_LOG_ERR, request->connection->log, 0, "CONTEXT STATUS 'default' REACHED.");

      result = ngx_http_next_body_filter( request, in_chain );
      if ( result == NGX_OK ) result = NGX_ERROR; 
      return result; 
  }

  return ngx_http_next_body_filter( request, in_chain );
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
