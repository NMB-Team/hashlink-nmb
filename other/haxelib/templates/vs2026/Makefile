VSWHERE?="C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
VS_VERSION?="[18.0,19.0)"
MS_BUILD?="$(shell $(VSWHERE) -requires Microsoft.Component.MSBuild -products "*" -latest -version $(VS_VERSION) -find MSBuild/Current/Bin/MSBuild.exe)"
PLATFORM?=x64
CONFIG?=Release

all: build

build:
	$(MS_BUILD) ::name::.sln -t:::name:: -nologo -verbosity:minimal -property:Configuration=$(CONFIG) -property:Platform=$(PLATFORM)