FROM gliderlabs/alpine:3.1

MAINTAINER SmartKit Labs <z@smartkit.info>



RUN apk-install openssl
RUN apk-install g++ cmake

COPY . /app
WORKDIR /app

#make and install
RUN ./configure \ 
	&& make \
	&& make install
