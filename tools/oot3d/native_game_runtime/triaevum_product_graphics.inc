R"json({
  "Preset": "Custom",
  "SchemaVersion": 11,
  "FrameRate": {
    "Mode": "Interpolated2x"
  },
  "Camera": {
    "FovMultiplier": 1.1
  },
  "Effects": {
    "Toon": {
      "BandSoftness": 0.10599999874830246,
      "CustomLightBands": true,
      "LightBandLevels": [
        0.1679999977350235,
        0.3333333432674408,
        0.6666666865348816,
        1,
        1,
        1
      ],
      "LightBandThresholds": [
        0.1666666716337204,
        0.5,
        0.8333333134651184,
        1,
        1
      ],
      "LightBands": 4,
      "Mode": "PicaMaterial",
      "OutlineDepthSensitivity": 1.7200000286102295,
      "OutlineEnabled": true,
      "OutlineNormalSensitivity": 0.019999999552965164,
      "OutlineOpacity": 1,
      "OutlineSoftness": 1,
      "OutlineTint": [
        0.029999999329447746,
        0.03999999910593033,
        0.05999999865889549
      ],
      "OutlineWidth": 0.949999988079071,
      "RimStrength": 0.3400000035762787,
      "RimTint": [
        1,
        0.9300000071525574,
        0.7200000286102295
      ],
      "RimWidth": 3.1700000762939453,
      "Saturation": 1.090000033378601,
      "ShadowStrength": 1,
      "ShadowTint": [
        0.47999998927116394,
        0.5600000023841858,
        0.7200000286102295
      ]
    }
  },
  "Grass": {
    "Appearance": {
      "BladeCurvature": 0.6399999856948853,
      "BladeDroop": 0.3499999940395355,
      "BladeSegments": 5,
      "BladeTwistDegrees": 141.0,
      "HeightScale": 1.149999976158142,
      "ReceiveFog": true,
      "ReceiveLighting": true,
      "RootColor": [
        0.09882351756095886,
        0.1899999976158142,
        0.03500000014901161
      ],
      "ShapeVariation": 0.3799999952316284,
      "TextureColorInfluence": 0.75,
      "TextureRootBrightness": 1.0099999904632568,
      "TextureTipBrightness": 1.7400000095367432,
      "TipColor": [
        0.19058825075626373,
        0.7200000286102295,
        0.12000001966953278
      ],
      "ToonRimEnabled": false,
      "ToonRimFadeEnd": 901.0,
      "ToonRimFadeStart": 0.0
    },
    "Budget": {
      "DrawDistance": 50000.0,
      "MaxInstancesPerRoom": 500000
    },
    "Generation": {
      "BladeHeightMax": 12.510000228881836,
      "BladeHeightMin": 7.679999828338623,
      "BladeWidthMax": 1.940000057220459,
      "BladeWidthMin": 1.7999999523162842,
      "ClusterCoverage": 0.6499999761581421,
      "ClusterScale": 300.0,
      "ClusterStrength": 0.0,
      "IndividualRandomness": 1.0,
      "InstancesPerSquareMeter": 1123.9000244140625,
      "MinimumSpacing": 0.0,
      "Seed": 1
    },
    "LinkInteraction": {
      "ColliderHeightMultiplier": 1.690000057220459,
      "ColliderRadiusMultiplier": 1.190000057220459,
      "CollisionPush": 1.659999966621399,
      "Damping": 1.4600000381469727,
      "FieldRadius": 600.0,
      "FieldResolution": 256,
      "MaximumBend": 0.8500000238418579,
      "RecoverySeconds": 1.0,
      "VelocityResponse": 0.8700000047683716,
      "VerticalMargin": 69.0
    },
    "Performance": {
      "CullingClusterSize": 25.0,
      "DensityFadeFraction": 1.0,
      "DrawFadeFraction": 0.15000000596046448,
      "FarBladeSegments": 1,
      "FarDensity": 0.6200000047683716,
      "FarTuftBladeCount": 3,
      "FarTuftDensity": 0.10000000149011612,
      "FarTuftSpread": 4.0,
      "FarTuftsEnabled": true,
      "FrustumCulling": true,
      "LodEndFraction": 1.0,
      "LodReferenceDistance": 1668.0,
      "LodStartFraction": 1.0,
      "SegmentLodEndDistance": 1000.0,
      "SegmentLodSoftness": 0.75,
      "SegmentLodStartDistance": 501.0,
      "TuftTransitionFraction": 0.49000000953674316
    },
    "Quality": "Custom",
    "Sources": [
      {
        "Channel": "Green",
        "InputBlack": 0.0,
        "InputWhite": 0.25200000405311584,
        "Invert": false,
        "MaximumSlopeDegrees": 65.0,
        "NormalOffset": -0.15000000596046448,
        "OutputBlack": 0.0,
        "OutputWhite": 1.0,
        "ResponseExponent": 8.0,
        "RuleId": 1,
        "Target": {
          "AssetName": "",
          "Height": 256,
          "MapperSlotMask": 7,
          "Rgba8Hash": "be15aff93dfdcd88",
          "Width": 256
        },
        "Wrap": "Material"
      },
      {
        "Channel": "Green",
        "InputBlack": 0.7590000033378601,
        "InputWhite": 0.8360000252723694,
        "Invert": true,
        "MaximumSlopeDegrees": 48.0,
        "NormalOffset": 0.5,
        "OutputBlack": 0.0,
        "OutputWhite": 1.0,
        "ResponseExponent": 1.0,
        "RuleId": 2,
        "Target": {
          "AssetName": "",
          "Height": 128,
          "MapperSlotMask": 7,
          "Rgba8Hash": "2321986eb9820c29",
          "Width": 128
        },
        "Wrap": "Material"
      },
      {
        "Channel": "Green",
        "InputBlack": 0.0,
        "InputWhite": 0.5720000267028809,
        "Invert": false,
        "MaximumSlopeDegrees": 48.0,
        "NormalOffset": 0.5,
        "OutputBlack": 0.0,
        "OutputWhite": 1.0,
        "ResponseExponent": 8.0,
        "RuleId": 3,
        "Target": {
          "AssetName": "",
          "Height": 256,
          "MapperSlotMask": 7,
          "Rgba8Hash": "4b8941fd174516b0",
          "Width": 256
        },
        "Wrap": "Material"
      },
      {
        "Channel": "Green",
        "InputBlack": 0.48399999737739563,
        "InputWhite": 0.48500001430511475,
        "Invert": true,
        "MaximumSlopeDegrees": 48.0,
        "NormalOffset": 0.5,
        "OutputBlack": 0.0,
        "OutputWhite": 1.0,
        "ResponseExponent": 8.0,
        "RuleId": 4,
        "Target": {
          "AssetName": "",
          "Height": 256,
          "MapperSlotMask": 7,
          "Rgba8Hash": "0a29e93a3b0742b3",
          "Width": 256
        },
        "Wrap": "Material"
      },
      {
        "Channel": "Green",
        "InputBlack": 0.49799999594688416,
        "InputWhite": 0.49900001287460327,
        "Invert": true,
        "MaximumSlopeDegrees": 48.0,
        "NormalOffset": 0.5,
        "OutputBlack": 0.0,
        "OutputWhite": 1.0,
        "ResponseExponent": 8.0,
        "RuleId": 5,
        "Target": {
          "AssetName": "",
          "Height": 128,
          "MapperSlotMask": 7,
          "Rgba8Hash": "bd769b9ce136d73a",
          "Width": 256
        },
        "Wrap": "Material"
      },
      {
        "Channel": "Green",
        "InputBlack": 0.503000020980835,
        "InputWhite": 0.5040000081062317,
        "Invert": true,
        "MaximumSlopeDegrees": 48.0,
        "NormalOffset": 0.5,
        "OutputBlack": 0.0,
        "OutputWhite": 1.0,
        "ResponseExponent": 8.0,
        "RuleId": 6,
        "Target": {
          "AssetName": "",
          "Height": 128,
          "MapperSlotMask": 7,
          "Rgba8Hash": "d13528cd4896c851",
          "Width": 128
        },
        "Wrap": "Material"
      }
    ],
    "Wind": {
      "DirectionDegrees": 0.0,
      "GustFrequency": 0.3499999940395355,
      "GustStrength": 0.3499999940395355,
      "Randomness": 0.4399999976158142,
      "SpatialScale": 1.0,
      "Speed": 2.8399999141693115,
      "Strength": 0.5600000023841858,
      "Turbulence": 0.41999998688697815
    }
  }
})json"
