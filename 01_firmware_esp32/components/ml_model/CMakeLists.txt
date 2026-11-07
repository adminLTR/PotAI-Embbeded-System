set(RF_MODEL_BIN "${CMAKE_CURRENT_LIST_DIR}/model/rf_model.bin")

if(NOT EXISTS "${RF_MODEL_BIN}")
    message(FATAL_ERROR
        "Falta el modelo comprimido: ${RF_MODEL_BIN}\n"
        "Generelo con:  cd 03_model_ML && python export_model_esp32.py")
endif()

idf_component_register(
    SRCS "ml_model.c"
    INCLUDE_DIRS "include"
    REQUIRES log
    EMBED_FILES "model/rf_model.bin"
)
