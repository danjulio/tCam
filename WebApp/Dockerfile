# This Dockerfile builds an image for the tcam web application, for the AMD64 platform.

# Use a slim image of Debian to keep the image light.
FROM debian:stable-slim

# Define the URL for the pre-compiled web application archive.
ARG TCAM_LINUX_WEB_SERVER_ZIP_AMD64="https://github.com/danjulio/tCam/raw/main/WebApp/zip_files/Linux64.zip"

WORKDIR /app

RUN apt update && \
    # Install packages. Some are temporarily used and get removed at the end. \
    apt install wget ca-certificates unzip libunwind8 libglib2.0-0 --no-install-recommends -y && \
    cd /tmp/ && \
    wget "$TCAM_LINUX_WEB_SERVER_ZIP_AMD64" && \
    unzip *.zip && \
    mv Linux*/tcam_web/* /app/ && \
    chmod 755 /app/tcam_web && \
    ln -s /app/tcam_web /usr/local/bin/ && \
    # Clean up and remove unnecessary packages, files and directories. \
    apt purge wget ca-certificates unzip -y && \
    apt autoremove -y && \
    apt clean && \
    rm -rf /tmp/* && \
    rm -rf /var/lib/apt/lists/* && \
    rm -rf /var/cache/debconf/* && \
    rm -rf /usr/share/doc/*

# Volumes contain things that we would like to keep, even if the container is terminated.
VOLUME /tcam_config
VOLUME /tcam_images

# `EXPOSE` statements are for documentation purposes in Dockerfiles.
# We want to expose port 80 inside the container to any open port of your choosing on the host.
EXPOSE 80

# Command to run `tcam_web` with custom options.
CMD [ "sh", "-c", "tcam_web -c /tcam_config/config.json -i /tcam_images --port=80" ]

