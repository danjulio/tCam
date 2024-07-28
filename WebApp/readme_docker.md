Hosting The Web Application With Docker
=====

#### Description

For portability and extreme ease of deployment, it is possible to host the web application in a container.

In this case, I have tested running the Linux x64 app within Docker, on both Windows as well as Linux hosts.


#### Instructions

1. Download the `Dockerfile` in this directory.

2. Build the Docker image, tagging the image as, for example, `tcam-docker`.

```
docker build -t tcam-docker -f Dockerfile .
```

3. Spin up the tcam web application container from the image.

We specify volume mounting two separate directories; one to contain the tcam config, and one to contain streamed images.

The reason for doing so is to allow the settings and images to be saved if the container is terminated.

We also specify port 9000 on the host to be mapped to port 80 within the container.

```
mkdir -p config images

docker run -itd \
    --name tcam-docker \
    -p 9000:80 \
    --volume ./config:/tcam_config:Z \
    --volume ./images:/tcam_images:Z \
    tcam-docker
```
4. The web application will be visible on your host machine at the host port specified.

Open a browser window and navigate to `http://localhost:9000`.

If you see the site load, you are in business!

