# Pipeline tool for building texture files.

licenses(["notice"])  # Apache 2.0

cc_library(
    name = "encode_astc",
    srcs = [
        "encode_astc.cc",
    ],
    hdrs = [
        "encode_astc.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//third_party/astc_encoder:astc_external",
        "//lullaby/modules/render",
        "//lullaby/util:common_types",
        "//lullaby/util:logging",
    ],
)

cc_library(
    name = "encode_jpg",
    srcs = [
        "encode_jpg.cc",
    ],
    hdrs = [
        "encode_jpg.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//third_party/libjpeg_turbo:jpeg",
        "//lullaby/modules/render",
        "//lullaby/util:common_types",
        "//lullaby/util:logging",
    ],
)

cc_library(
    name = "encode_ktx",
    srcs = [
        "encode_ktx.cc",
    ],
    hdrs = [
        "encode_ktx.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//third_party/etc2comp:etclib",
        "//lullaby/modules/render",
        "//lullaby/util:common_types",
        "//lullaby/util:logging",
    ],
)

cc_library(
    name = "encode_png",
    srcs = [
        "encode_png.cc",
    ],
    hdrs = [
        "encode_png.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//lullaby/modules/render",
        "//lullaby/util:common_types",
        "//lullaby/util:logging",
        "@stb//:stb",
    ],
)

cc_library(
    name = "encode_webp",
    srcs = [
        "encode_webp.cc",
    ],
    hdrs = [
        "encode_webp.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@libwebp//:webp_encode",
        "//lullaby/modules/render",
        "//lullaby/util:common_types",
        "//lullaby/util:logging",
    ],
)

cc_binary(
    name = "texture_pipeline",
    srcs = [
        "main.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":encode_astc",
        ":encode_jpg",
        ":encode_ktx",
        ":encode_png",
        ":encode_webp",
        "//lullaby/modules/render",
        "//lullaby/util:arg_parser",
        "//lullaby/util:common_types",
        "//lullaby/util:filename",
        "//lullaby/util:logging",
        "//third_party/lullaby/tools/common",
    ],
)
