# A sample guide for participants of [Global Software Analysis Competition](https://gsac.tech/)

Participants are free to modify the project files except the following ones (otherwise the evaluating system will fail).

* [```run.sh```](/example-analyzer/run.sh)
    * Can't be renamed or moved
    * Must take a bitcode file as an input(compiled with clang-12)
    * Output should be in SARIF format named ```report.sarif```
        * It must be generated in the same directory where ```run.sh``` is called
        * An example of result file format  (```report.sarif```)
          can be found [here](https://github.com/GSACTech/resources/blob/main/contest)
* [```Dockerfile```](/example-analyzer/Dockerfile)
    * Create a docker image where the analyzer is copied into ```/root``` directory
    * Install all required packages and build the analyzer
