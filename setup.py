from re import L
import setuptools
import sys
import platform

with open("README.md", encoding="utf-8") as f:
    long_description = f.read()

with open("./src/objtracker/__init__.py") as f:
    for line in f.readlines():
        if line.startswith("__version__"):
            delim = '"' if '"' in line else "'"
            version = line.split(delim)[1]
            break
    else:
        print("Can't find version! Stop Here!")
        exit(-1)

# Determine which attach binary to take into package
package_data = {}

setuptools.setup(
    name="objtracker",
    version=version,
    author="chuiwaiming",
    author_email="anothersm@163.com",
    long_description=long_description,
    url="https://github.com/CLAY-zhao/obj-tracker",
    packages=setuptools.find_packages("src"),
    package_dir={"": "src"},
    package_data=package_data,
    ext_modules=[
        setuptools.Extension(
            "objtracker.tracker",
            sources=[
                "src/objtracker/modules/utils.c",
                "src/objtracker/modules/objtracker.c"
            ],
            extra_compile_args={"win32": []}.get(sys.platform, ["-Werror", "-std=c99"]),
            extra_link_args={"win32": []}.get(sys.platform, ["-lpthread"])
        )
    ],
    classifiers=[
        "Development Status :: 4 - Beta",
        "Programming Language :: Python :: 3.6",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Apache Software License",
        "Operating System :: MacOS",
        "Operating System :: POSIX :: Linux",
        "Operating System :: Microsoft :: Windows",
        "Topic :: Software Development :: Quality Assurance",
        "Topic :: Software Development :: Bug Tracking",
        "Topic :: System :: Logging"
    ],
    python_requires=">=3.6"
)