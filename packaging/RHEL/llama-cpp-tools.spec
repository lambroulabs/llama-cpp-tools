Name:           llama-cpp-tools
Version:        0.1.0
Release:        1%{?dist}
Summary:        C++ llama.cpp function-calling tool registry

License:        MIT
URL:            https://github.com/lambroulabs/llama-cpp-tools
Source0:        %{url}/archive/refs/tags/v%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  nlohmann-json-devel
Requires:       glibc

%description
llama-cpp-tools provides a C++ registry for function calling tools that
integrates with llama.cpp and related projects.

%package devel
Summary:        Development files for llama-cpp-tools
Requires:       %{name} = %{version}-%{release}
Requires:       nlohmann-json-devel

%description devel
The llama-cpp-tools development package contains headers, CMake config
files, and other resources needed to build software against llama-cpp-tools.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=%{_prefix}
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%doc README.md
%{_bindir}/*
%{_libdir}/libllama-cpp-tools.so.*

%files devel
%{_includedir}/llama-cpp-tools/
%{_libdir}/libllama-cpp-tools.so
%{_libdir}/cmake/llama-cpp-tools/

%changelog
* Tue Sep 02 2025 Alexander Lambrou <alexanderlambrou0602@gmail.com> - 0.1.0-1
- Initial RPM release
