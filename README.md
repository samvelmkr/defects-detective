# example analyzer tool

This is an example project to help you create your own analyzer.
You can read the [PARTICIPANTS_GUIDE.md](./PARTICIPANTS_GUIDE.md) to know what modifications are available.

## Build

### Required packages

To get all the requirements, run the following script

```shell
sudo /path/to/example-analyzer/scripts/install_required_packages.sh
```

Here is the list of requirements

* python3
* python3-pip
* cmake
* clang-12
* llvm-12
* llvm-12-dev

### To build, ensure that all requirements are installed and then run.

```shell
/path/to/example-analyzer/scripts/build.sh
```

### Docker image

To use the tool in Docker, follow the instructions below.

* [***Install docker***](https://docs.docker.com/engine/install/)

* After running this command, the Docker image will be created, containing the compiled analyzer
  in the `/root` directory, along with all the necessary packages installed.
    ```shell
    docker build -t image_name /path/to/example-analyzer
    ```
* To run a container based on that image, use the following command

    ```shell
    docker run --name container-name -it image_name
    ```

## Usage

To run the tool, use the [run.sh](./run.sh) script.

```shell
/path/to/example-analyzer/run.sh /path/to/bitcode.bc
```

* The tool takes a bitcode file as an input.
* Output is a sarif file, which contains all reports.
