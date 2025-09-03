Name:           llama-cpp-tools
Version:        0.1.0
Release:        1%{?dist}
Summary:        C++ llama.cpp function-calling tool registry

License:        MIT
URL:            https://github.com/lambroulabs/llama-cpp-tools
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  json-devel
BuildRequires:  catch-devel
BuildRequires:  gcc-c++
Requires:       glibc

%description
llama-cpp-tools provides a C++ registry for function calling tools that
integrates with llama.cpp and related projects.

%package devel
Summary:        Development files for llama-cpp-tools
Requires:       %{name} = %{version}-%{release}
Requires:       json-devel
Requires:       catch2-devel

%description devel
The llama-cpp-tools development package contains headers, CMake config
files, and other resources needed to build software against llama-cpp-tools.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=%{_prefix}
%cmake_build

%install
rm -rf %{buildroot}
cmake --install /path/to/build --prefix=/usr --destdir=%{buildroot}

%files
%license LICENSE
%{_libdir}/libllama_cpp_tools.so*
%{_libdir}/cmake/llama-cpp-tools/*
%{_libdir}/pkgconfig/llama-cpp-tools.pc
%{_includedir}/llama_cpp_tools/*

%files devel
%{_includedir}/llama-cpp-tools/
%{_libdir}/libllama-cpp-tools.so
%{_libdir}/cmake/llama-cpp-tools/

%changelog
* Tue Sep 02 2025 Alexander Lambrou <alexanderlambrou0602@gmail.com> - 0.1.0-1
- Initial RPM release
