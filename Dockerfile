FROM yutakakinjyo/gtest
#FROM gliderlabs/alpine:3.1

MAINTAINER SmartKit Labs <z@smartkit.info>


RUN yum update -y
RUN touch /var/lib/rpm/*
RUN yum install -y openssl
RUN yum install g++ cmake

COPY . /app
WORKDIR /app

#make and install
RUN  make
