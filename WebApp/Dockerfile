FROM debian:stable-slim

RUN apt update && \
    apt install wget unzip libunwind8 libglib2.0-0 --no-install-recommends -y && \
    apt clean

WORKDIR /app

ARG TCAM_LINUX_WEB_SERVER_ZIP="https://github.com/danjulio/tCam/raw/main/WebApp/zip_files/Linux64.zip"

RUN cd /tmp/ && \
    wget --no-check-certificate $TCAM_LINUX_WEB_SERVER_ZIP && \
    unzip *.zip && \
    mv Linux\ 64\ bit/tcam_web/* /app/ && \
    rm -rf /tmp/*

RUN chmod 755 /app/tcam_web && \
    ln -s /app/tcam_web /usr/local/bin/

VOLUME /tcam_config
VOLUME /tcam_images

EXPOSE 9000

CMD [ "sh", "-c", "tcam_web -c /tcam_config/config.json -i /tcam_images --port=80" ]

