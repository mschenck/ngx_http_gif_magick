ngx_http_gif_magick
----

# Configuration settings

## Enabling
_default: disabled_

    gif_magick              on;   

## Buffer size
_default: 20m_

Limits the maximum image size

    gif_magick_buffer       20m; 
 
## Resizing

If you select only `gif_magick_width` or `gif_magick_height`, then the aspect ratio will be retained.  You must specify at least one.

### Resize width

    gif_magick_width        420;
 
### Resize height

    gif_magick_height       320;


