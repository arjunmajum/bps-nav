import torch
import torchvision
import v4r_example
import sys
import os

if len(sys.argv) != 2:
    print("test.py path/to/stokes.glb")
    os.exit(1)

script_dir = os.path.dirname(os.path.realpath(__file__))
views = script_dir + "/stokes_views"
out_dir = script_dir + "/out"
os.makedirs(out_dir, exist_ok=True)

renderer = v4r_example.V4RExample(sys.argv[1], views)

print("Initialized and loaded")

tensor = renderer.getColorTensor()
print(tensor.shape)


for i in range(5):
    print(f"Rendering batch {i}")
    sync = renderer.render()
    sync.wait()

    # Transpose to NCHW
    nchw = tensor.permute(0, 3, 1, 2)
    # Chop off alpha channel
    rgb = nchw[:, 0:3, :, :]

    for j in range(32):
        img = torchvision.transforms.ToPILImage()(rgb[j].cpu())
        img.save(f"{out_dir}/{i}_{j}.png")

print("Done")