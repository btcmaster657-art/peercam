{
  "targets": [
    {
      "target_name": "vcam",
      "sources": ["vcam.cc"],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"],
      "conditions": [
        ["OS=='win'", {
          "sources": ["vcam_win.cc"],
          "libraries": ["-lole32", "-loleaut32", "-lstrmiids", "-ladvapi32"]
        }],
        ["OS=='linux'", {
          "sources": ["vcam_linux.cc"],
          "libraries": []
        }]
      ]
    }
  ]
}
