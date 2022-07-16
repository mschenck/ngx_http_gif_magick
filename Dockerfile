FROM alpine:latest

ARG nginx_version:1.22.0

ENV NGINX_VERSION=$nginx_version
ENV NGINX_VERSION=1.22.0

EXPOSE 80

RUN apk --update add openssl-dev pcre-dev zlib-dev wget build-base imagemagick6-dev

RUN cd /tmp ;\
    wget -O nginx-$NGINX_VERSION.tar.gz https://github.com/nginx/nginx/archive/refs/tags/release-$NGINX_VERSION.tar.gz ; \
    tar -zxf nginx-$NGINX_VERSION.tar.gz


COPY ngx_http_gif_magick_module.c config /tmp/nginx-release-$NGINX_VERSION/

RUN cd /tmp/nginx-release-$NGINX_VERSION ; \
    ./auto/configure \
      --prefix=/var/www/html \
      --sbin-path=/usr/sbin/nginx \
      --conf-path=/etc/nginx/nginx.conf \
      --http-log-path=/var/log/nginx/access.log \
      --error-log-path=/var/log/nginx/error.log \
      --with-pcre  \
      --lock-path=/var/lock/nginx.lock \
      --pid-path=/var/run/nginx.pid \
      --with-http_ssl_module \
      --modules-path=/etc/nginx/modules \
      --with-http_v2_module \
      --add-module=. && \
      sed -ie 's/-Werror//' objs/Makefile && \
      make && \
      make install 

COPY docker_assets/nginx.conf /etc/nginx/nginx.conf
COPY docker_assets/index.html /var/www/html/html/

ENTRYPOINT ["/usr/sbin/nginx"]
