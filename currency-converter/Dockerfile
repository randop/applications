FROM gcr.io/distroless/cc:nonroot

ADD build/currency-converter /

# register application service
COPY resources/docker/app.run /etc/s6-overlay/s6-rc.d/app/run
RUN echo 'longrun' > /etc/s6-overlay/s6-rc.d/app/type && \
    touch /etc/s6-overlay/s6-rc.d/user/contents.d/app

ENTRYPOINT ["/init"]
