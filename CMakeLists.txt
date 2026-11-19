cmake_minimum_required(VERSION 3.16)
project(labka VERSION 0.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
message(STATUS "Qt: ${Qt6_VERSION}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(SSH REQUIRED libssh)
message(STATUS "libssh: ${SSH_VERSION}")

# ─────────────────────────────────────────────
# BOTAN 3  (búsqueda en cadena, con diagnóstico)
#   0) -DBOTAN_ROOT=/ruta
#   1) find_package(Botan3)
#   2) external/botan/build  (.a o .so)
#   3) /usr/local, /usr  (include/botan-3, lib, lib64, multiarch)
#   4) pkg-config botan-3 / botan-2
# ─────────────────────────────────────────────
set(BOTAN_ROOT "" CACHE PATH "Raíz de Botan 3 (opcional)")

set(BOTAN_INCLUDE_DIR "")
set(BOTAN_LIBRARY     "")
set(BOTAN_IS_STATIC   FALSE)
set(BOTAN_LINK_DIRS   "")

find_package(Botan3 QUIET)
if(Botan3_FOUND)
    message(STATUS "Botan 3: paquete CMake del sistema")
    set(BOTAN_INCLUDE_DIR "${Botan3_INCLUDE_DIRS}")
    set(BOTAN_LIBRARY     "${Botan3_LIBRARIES}")
else()
    set(_botan_roots
        "${BOTAN_ROOT}"
        "${CMAKE_SOURCE_DIR}/external/botan/build"
        "/usr/local"
        "/usr"
    )

    foreach(_r ${_botan_roots})
        if("${_r}" STREQUAL "")
            continue()
        endif()

        if(NOT BOTAN_INCLUDE_DIR)
            foreach(_inc "${_r}/include/botan-3" "${_r}/include/botan-2" "${_r}/include")
                if(EXISTS "${_inc}/botan/version.h")
                    set(BOTAN_INCLUDE_DIR "${_inc}")
                    break()
                endif()
            endforeach()
        endif()

        if(NOT BOTAN_LIBRARY)
            foreach(_ld "${_r}/lib" "${_r}/lib64" "${_r}/lib/x86_64-linux-gnu")
                foreach(_n libbotan-3.a libbotan-3.so libbotan-2.a libbotan-2.so)
                    if(EXISTS "${_ld}/${_n}")
                        set(BOTAN_LIBRARY "${_ld}/${_n}")
                        set(BOTAN_LINK_DIRS "${_ld}")
                        if(_n MATCHES "\\.a$")
                            set(BOTAN_IS_STATIC TRUE)
                        endif()
                        break()
                    endif()
                endforeach()
                if(BOTAN_LIBRARY)
                    break()
                endif()
            endforeach()
        endif()
    endforeach()

    if(BOTAN_INCLUDE_DIR AND BOTAN_LIBRARY)
        message(STATUS "Botan 3 include: ${BOTAN_INCLUDE_DIR}")
        message(STATUS "Botan 3 lib:     ${BOTAN_LIBRARY}")
        if(BOTAN_IS_STATIC)
            message(STATUS "Botan 3: ESTÁTICO (no se dependerá del paquete compartido)")
        endif()
    else()
        pkg_check_modules(BOTAN_PC QUIET botan-3)
        if(NOT BOTAN_PC_FOUND)
            pkg_check_modules(BOTAN_PC QUIET botan-2)
        endif()

        if(BOTAN_PC_FOUND)
            set(BOTAN_INCLUDE_DIR "${BOTAN_PC_INCLUDE_DIRS}")
            set(BOTAN_LIBRARY     "${BOTAN_PC_LIBRARIES}")
            set(BOTAN_LINK_DIRS   "${BOTAN_PC_LIBRARY_DIRS}")
            message(STATUS "Botan: pkg-config ${BOTAN_PC_VERSION}")
        else()
            message(STATUS "---- Diagnóstico Botan ----")
            foreach(_r ${_botan_roots})
                if(NOT "${_r}" STREQUAL "")
                    message(STATUS "  revisado: ${_r}/include/botan-3/botan/version.h")
                    message(STATUS "  revisado: ${_r}/lib/libbotan-3.{a,so}")
                endif()
            endforeach()
            message(FATAL_ERROR
                "No se encontró Botan 3.\n"
                "  Soluciones:\n"
                "   1) Compilarlo:  cd external/botan && ./configure.py && make -j$(nproc)\n"
                "      (debe existir external/botan/build/include/botan-3 y build/lib/libbotan-3.a)\n"
                "   2) sudo apt install libbotan-3-dev\n"
                "   3) cmake .. -DBOTAN_ROOT=/ruta/a/botan/install\n")
        endif()
    endif()
endif()

# ─────────────────────────────────────────────
# FUENTES
# ─────────────────────────────────────────────
set(LABKA_SOURCES
    src/main.cpp
    src/core/labka_core.cpp
    src/core/ssh_client.cpp
    src/core/auth_manager.cpp
    src/ui/labka_ui.cpp
    src/ui/main_window.cpp
)

set(LABKA_HEADERS
    src/core/labka_core.h
    src/core/ssh_client.h
    src/core/auth_manager.h
    src/ui/labka_ui.h
)

add_executable(rubic-labka ${LABKA_SOURCES} ${LABKA_HEADERS})

target_include_directories(rubic-labka PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/ui
    ${SSH_INCLUDE_DIRS}
    ${BOTAN_INCLUDE_DIR}
)

if(BOTAN_LINK_DIRS)
    target_link_directories(rubic-labka PRIVATE ${BOTAN_LINK_DIRS})
endif()
if(SSH_LIBRARY_DIRS)
    target_link_directories(rubic-labka PRIVATE ${SSH_LIBRARY_DIRS})
endif()

target_link_libraries(rubic-labka PRIVATE
    Threads::Threads
    Qt6::Widgets
    Qt6::Svg
    ${SSH_LIBRARIES}
    ${BOTAN_LIBRARY}
    ${CMAKE_DL_LIBS}
)

target_compile_definitions(rubic-labka PRIVATE
    LABKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    LABKA_VERSION="${PROJECT_VERSION}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rubic-labka PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ─────────────────────────────────────────────
# ASSETS junto al binario
# ─────────────────────────────────────────────
if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
    file(COPY "${CMAKE_SOURCE_DIR}/assets" DESTINATION "${CMAKE_BINARY_DIR}")

    add_custom_command(TARGET rubic-labka POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:rubic-labka>/assets"
        COMMENT "Copiando assets/ junto al binario"
    )
endif()

# ─────────────────────────────────────────────
# TRADUCCIONES   →  make translations
# ─────────────────────────────────────────────
set(LABKA_TS_SCRIPT "${CMAKE_SOURCE_DIR}/translations/make_translations.py")
find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_FOUND AND EXISTS "${LABKA_TS_SCRIPT}")
    add_custom_target(translations
        COMMAND ${Python3_EXECUTABLE} "${LABKA_TS_SCRIPT}"
                --qm-dir "$<TARGET_FILE_DIR:rubic-labka>/translations"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Generando traducciones (.ts + .qm)"
        VERBATIM
    )
else()
    add_custom_target(translations
        COMMAND ${CMAKE_COMMAND} -E echo
                "Falta Python3 o translations/make_translations.py"
    )
endif()

add_custom_command(TARGET rubic-labka POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:rubic-labka>/translations"
)

if(EXISTS "${CMAKE_SOURCE_DIR}/translations")
    file(GLOB LABKA_QM_FILES "${CMAKE_SOURCE_DIR}/translations/*.qm")
    foreach(_qm ${LABKA_QM_FILES})
        add_custom_command(TARGET rubic-labka POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_qm}" "$<TARGET_FILE_DIR:rubic-labka>/translations/"
        )
    endforeach()
endif()

# ─────────────────────────────────────────────
# INSTALACIÓN
# ─────────────────────────────────────────────
install(TARGETS rubic-labka RUNTIME DESTINATION bin)

# ★ Logo SVG (preferido). Si no está, cae al PNG.
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/LOGO/Rubik.svg")
    install(FILES assets/LOGO/Rubik.svg
            DESTINATION share/icons/hicolor/scalable/apps
            RENAME rubic-labka.svg)
elseif(EXISTS "${CMAKE_SOURCE_DIR}/assets/LOGO/Rubik.png")
    install(FILES assets/LOGO/Rubik.png
            DESTINATION share/icons/hicolor/256x256/apps
            RENAME rubic-labka.png)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/"
            DESTINATION share/rubic-labka/assets)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/translations")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/translations/"
            DESTINATION share/rubic-labka/translations
            FILES_MATCHING PATTERN "*.qm")
endif()

set(DESKTOP_FILE "${CMAKE_BINARY_DIR}/rubic-labka.desktop")
file(WRITE "${DESKTOP_FILE}"
"[Desktop Entry]\n"
"Name=Rubic Labka\n"
"Comment=SSH & SFTP Manager by Wallace Corp\n"
"Exec=rubic-labka\n"
"Icon=rubic-labka\n"
"Terminal=false\n"
"Type=Application\n"
"Categories=Network;Utility;\n"
)
install(FILES "${DESKTOP_FILE}" DESTINATION share/applications)

if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
    install(FILES LICENSE DESTINATION share/licenses/rubic-labka)
endif()

# ─────────────────────────────────────────────
# CPACK (.deb)
# ─────────────────────────────────────────────
set(LABKA_DEB_DEPENDS "libc6, libstdc++6, libqt6widgets6, libqt6svg6, libssh-4")
if(NOT BOTAN_IS_STATIC)
    set(LABKA_DEB_DEPENDS "${LABKA_DEB_DEPENDS}, libbotan-3-0")
endif()

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "rubic-labka")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Niander Wallace <wallace@rubic.online>")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Niander Wallace")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "${LABKA_DEB_DEPENDS}")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "openssh-client, gvfs-backends | gvfs-fuse, remmina | tigervnc-viewer | krdc")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "sshpass, x11vnc")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "SSH & SFTP Manager for the Wallace Corp Network")
set(CPACK_PACKAGE_DESCRIPTION
"Rubic-Labka: mapa visual de la red local, credenciales SSH cifradas con
AES-256-GCM y llave Argon2id, auto-reconexion, terminal, explorador SFTP
y control remoto VNC en un clic.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://rubic.online")
set(CPACK_PACKAGE_FILE_NAME "rubic-labka_${PROJECT_VERSION}_amd64")

if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
endif()

include(CPack)