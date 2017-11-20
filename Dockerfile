FROM yutakakinjyo/gtest
#FROM gliderlabs/alpine:3.1

MAINTAINER SmartKit Labs <z@smartkit.info>


RUN yum update -y
RUN touch /var/lib/rpm/*
RUN yum install -y openssl openssl-devel libssl-devel
RUN yum install g++ cmake

#ImageMagick
RUN yum install -y gcc ImageMagick ImageMagick-devel


COPY . /app
WORKDIR /app

#make and install
RUN  make
