add_executable(KeyManager km_main.cc ../security/km_master_key.cc)
target_link_libraries(KeyManager ${KM_OBJ} ${LINK_OBJ})
add_executable(ClientMain client_main.cc
        ../../include/chunker/gear_poly.h
        ../chunker/gear_poly.cpp
        ../../include/chunker/odess_util.h
        ../../include/chunker/odess_simple.h
        ../chunker/odess_simple.cpp
        ../../include/chunker/n_transform.h
        ../chunker/n_transform.cpp
        ../../include/chunker/n_transform_gear.h
        ../chunker/n_transform_gear.cc
        ../../include/chunker/new_minhash.h
        ../chunker/new_minhash.cpp
        ../../include/chunker/odess_start.h
        ../chunker/odess_start.cpp
)
target_link_libraries(ClientMain ${CLIENT_OBJ} ${LINK_OBJ})
add_executable(TestMain main_test.cc)
target_link_libraries(TestMain ${CLIENT_OBJ} ${LINK_OBJ})
add_executable(ServerMain server_main.cc)
target_link_libraries(ServerMain ${SERVER_OBJ} ${LINK_OBJ})
