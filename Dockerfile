FROM ubuntu:20.04 AS example_analyzer

ENV DIR=/root/example-analyzer

COPY ./ "$DIR"
RUN "$DIR"/scripts/install_required_packages.sh && "$DIR"/scripts/build.sh
