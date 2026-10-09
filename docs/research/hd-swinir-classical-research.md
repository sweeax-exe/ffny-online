# SwinIR classical SR audition research

Research date: 2026-10-08. Scope: official sources and inference design only; no installation, checkpoint download, GPU inference, or game run performed for this report.

SwinIR classical SR is a useful additional candidate for the owner's original-art-preserving audition. Acceptance still depends on visual comparison of the actual tattoos, logos, and atlas regions. It should remain a separately labelled candidate alongside Lanczos and the existing models.

## Official checkpoint provenance

The [official SwinIR repository](https://github.com/JingyunLiang/SwinIR) identifies itself as the authors' PyTorch implementation. Its [README](https://github.com/JingyunLiang/SwinIR/blob/main/README.md) maps `001_classicalSR_DIV2K_s48w8_SwinIR-M` to classical SR trained on DIV2K's 800 training images, with a 48-pixel training patch and an 8-pixel attention window. Training patch size does not impose a 48-pixel inference tile. The DF2K/s64 checkpoints are a different setting and should not silently replace the requested model.

Both requested weights are assets of the authors' [v0.0 release](https://github.com/JingyunLiang/SwinIR/releases/tag/v0.0); the [expanded asset listing](https://github.com/JingyunLiang/SwinIR/releases/expanded_assets/v0.0) shows:

| Scale | Official download | GitHub displayed size | Asset upload time UTC |
|---|---|---|---|
| 2x | [001_classicalSR_DIV2K_s48w8_SwinIR-M_x2.pth](https://github.com/JingyunLiang/SwinIR/releases/download/v0.0/001_classicalSR_DIV2K_s48w8_SwinIR-M_x2.pth) | 56.3 MB | 2021-08-26 05:47:59 |
| 4x | [001_classicalSR_DIV2K_s48w8_SwinIR-M_x4.pth](https://github.com/JingyunLiang/SwinIR/releases/download/v0.0/001_classicalSR_DIV2K_s48w8_SwinIR-M_x4.pth) | 56.8 MB | 2021-08-26 05:48:06 |

No publisher checksum was visible in the inspected release listing. Record a local SHA-256 after download, the exact URL, and the source revision in the audition manifest. Use each scale's own checkpoint: native 2x and 4x-downsampled-to-2x are different candidates.

## Task and architecture

The [paper, loss-function section](https://arxiv.org/html/2108.10257) describes classical/lightweight SR training with pixel L1 loss. Its real-world SR experiment adds perceptual and GAN losses and uses BSRGAN degradation. Thus the requested `001_classicalSR` weights are not the `003_realSR_*_GAN` family. Classical SR targets bicubic degradation; that assumption does not establish suitability for Xbox palette quantization, DXT artifacts, UV atlases, or fine ink strokes.

The [official KAIR training configuration](https://github.com/cszn/KAIR/blob/master/options/swinir/train_swinir_sr_classical.json) and [official inference constructor](https://github.com/JingyunLiang/SwinIR/blob/main/main_test_swinir.py#L117) specify:

| Parameter | Requested classical setting |
|---|---|
| `upscale` | 2 or 4, matching checkpoint |
| `in_chans`, `img_size`, `window_size`, `img_range` | 3, 48, 8, 1.0 |
| `depths` | `[6,6,6,6,6,6]` |
| `embed_dim`, `num_heads`, `mlp_ratio` | 180, `[6,6,6,6,6,6]`, 2 |
| `upsampler`, `resi_connection` | `pixelshuffle`, `1conv` |

This is six residual Swin Transformer blocks with six Transformer layers each. Load the checkpoint's `params` mapping when present, otherwise the bare state dictionary, with `strict=True`. Preserve `img_size=48` for checkpoint buffers; do not use `real_sr`, its reconstruction path, or its EMA-key convention. Start with `eval()`, FP32, and inference without gradients.

## Input, padding, and tiling

The [network source](https://github.com/JingyunLiang/SwinIR/blob/main/models/network_swinir.py#L589) defaults include token patch size 1, QKV bias, LayerNorm, no absolute position embedding, and patch normalization. It expects three RGB channels in the 0–1 range and internally subtracts/restores RGB mean `(0.4488,0.4371,0.4040)`. Do not apply a second mean normalization. Its size check reflects the bottom/right edges to multiples of eight and its output crops back to the input dimensions multiplied by scale.

The [official test script](https://github.com/JingyunLiang/SwinIR/blob/main/main_test_swinir.py) instead explicitly extends bottom/right with flipped copies, adding eight pixels even to already aligned dimensions, then crops. Its tiled path uses a window-aligned tile size, default overlap 32 input pixels, stride `tile-overlap`, and averages every output pixel covered by overlapping tiles. It retains full-size output and weight buffers on the input device. Require `0 <= overlap < effective_tile`; tiny textures need adequate external padding before reflection.

**CLI integration trap:** classical SR enumerates `folder_gt` and reads matching `folder_lq/<stem>x<scale>.<ext>` files. Passing only an arbitrary texture folder to `--folder_lq` is insufficient. Use a small local inference adapter around the official network rather than fabricate ground-truth images. Model-selection flags are `--task classical_sr --scale 2|4 --training_patch_size 48 --model_path <matching checkpoint>`.

## Proposed audition recipe and limits

These are project recommendations, not quality or speed claims from the authors:

1. Reuse the reviewed sample IDs and existing RGB/alpha split. Run SwinIR on RGB only. Resize alpha with the established conventional method; retain its source meaning and the chosen independent-channel mip policy.
2. Preserve category-specific external clamp/wrap padding and crop exactly `padding * scale`. Record whether inference used the reference script's flip extension or the network's reflection. Neither automatically guarantees tileable texture seams.
3. Use whole-image inference for small samples when memory permits. For larger samples, start with a tile divisible by eight and overlap 32; record both, and compare a manageable whole-image sample against the tiled result. Smaller tiles change context and may introduce differences despite overlap averaging. Do not infer corpus throughput from another GPU's measurements.
4. Keep the same alpha treatment, padding, and comparison magnification across candidates. Judge tattoo line connectivity and thickness, letter shapes, ring edges, color shifts, atlas boundaries, and in-game appearance. L1 training lowers the incentive for perceptual embellishment but does not guarantee exact original markings or restore missing text.
5. For known logos or available original font outlines, retain the reconstruction/vector option. A learned photo-restoration model is not evidence of exact typography. Keep Lanczos available per asset when it preserves the original design better.

## License and reproducibility

The [README license statement](https://github.com/JingyunLiang/SwinIR/blob/main/README.md#license-and-acknowledgement) and [repository LICENSE](https://github.com/JingyunLiang/SwinIR/blob/main/LICENSE) identify Apache-2.0 and request following the upstream Swin Transformer/KAIR licenses. The inspected release page does not state a separate checkpoint license; describe that provenance explicitly rather than invent one. Preserve upstream licensing and notices with copied source. This model license does not provide rights to the game's original images; the existing source-only repository policy still applies.

Keep installation and weights in the external tool/data area, independent of the repository's Python 3.13 environment. Capture Python, PyTorch/CUDA, timm, source revision, checkpoint digest, scale, padding policy, tile/overlap, alpha method, and sample IDs. Pin working versions after the primary agent's compatibility check; this research did not verify a local environment.
