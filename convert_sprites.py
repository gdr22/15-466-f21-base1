import numpy as np
import skimage.io

tile_size = 8
map_size = 16

tilemap_size = tile_size * map_size

print("Loading")

sprites          = skimage.io.imread("assets/Sprites.png")
sprite_palettes  = skimage.io.imread("assets/Sprite_Palettes.png")[:,:,0]
palettes         = skimage.io.imread("assets/Palettes.png")

#print("Sprites")
#print(sprites)
print("Sprite Palettes")
print(sprite_palettes)
#print("Palettes")
#print(palettes)

# Convert the palettes to 32-bit numbers
out = np.zeros((tilemap_size, tilemap_size)).astype(np.uint8)

palettes32 = np.zeros((8, 4)).astype(np.uint32)
palettes32 += palettes[:,:,0] * 16777216
palettes32 += palettes[:,:,1] * 65536
palettes32 += palettes[:,:,2] * 256
palettes32 += palettes[:,:,3]

# Convert the sprites to 32-bit numbers
sprites32 = np.zeros((tilemap_size, tilemap_size)).astype(np.uint32)

sprites32 += sprites[:,:,0] * 16777216
sprites32 += sprites[:,:,1] * 65536
sprites32 += sprites[:,:,2] * 256
sprites32 += sprites[:,:,3]

bits = np.asarray([1, 2, 4, 8, 16, 32, 64, 128]).astype(np.uint8)
out_bytes = bytes('img0', 'utf-8');
out_bytes += (tilemap_size * tilemap_size * 2 // 8).to_bytes(4, 'little');

print(out_bytes)

for y in range(map_size):
  for x in range(map_size):
    x_lo = x*tile_size
    x_hi = (x+1)*tile_size
    y_lo = y*tile_size
    y_hi = (y+1)*tile_size

    print(x_lo, x_hi, y_lo, y_hi)
    
    # Grab the tile's palette
    palette = palettes32[sprite_palettes[y][x]]
    
    # Grab the tile pixels
    tile = sprites32[y_lo:y_hi,x_lo:x_hi]

    # Compute the 4 color version of the pixel
    tile4 = np.zeros((tile_size, tile_size)).astype(np.uint8)
    for c in range(4):
      tile4 = np.where(tile[:,:] == palette[c], c, tile4)
    
    bit0 = tile4  % 2
    bit1 = tile4 // 2

    rows0 = []
    rows1 = []

    for row0, row1 in zip(bit0, bit1):
      rows0.append(np.dot(row0, bits))
      rows1.append(np.dot(row1, bits))
    
    rows0.reverse()
    rows1.reverse()

    out_bytes += (bytes(rows0) + bytes(rows1))

    out[y_lo:y_hi,x_lo:x_hi] = tile4
    #print(out[y_lo:y_hi,x_lo:x_hi])

#print(out_bytes);

file = open("assets/sprites.bin", "wb")
file.write(out_bytes)
file.close()


skimage.io.imsave("assets/sprites4.png", out * 64)