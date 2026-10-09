#!/usr/bin/env python3
"""Bake the user's six-second animated glTF2 GLB into bounded FalconOS RGB sprites.
No GPU or dynamic GLTF parser is needed in Ring0. Source parts are actual GLB.
This is deliberately a build-time baker, never an unsafe boot-time file parser.
"""
import base64, json, struct, pathlib, argparse
import cv2, numpy as np, trimesh

def load(path):
    with open(path,"rb") as f:
        magic,version,sz=struct.unpack("<4sII",f.read(12))
        if magic!=b"glTF" or version!=2 or sz!=path.stat().st_size:
            raise ValueError("invalid glTF2 header")
        length,kind=struct.unpack("<II",f.read(8))
        if kind!=0x4E4F534A:raise ValueError("no glTF json chunk")
        j=json.loads(f.read(length))
        n,kind=struct.unpack("<II",f.read(8))
        if kind!=0x004E4942:raise ValueError("no binary chunk")
        binary=f.read(n)
    return j,binary

def bake(src,out,count):
    j,binary=load(src)
    scene=trimesh.load(str(src),force="scene")
    node_names={v["name"]:i for i,v in enumerate(j["nodes"])}
    meshes={n["name"]:list(scene.geometry.values())[n["mesh"]]
            for n in j["nodes"] if "mesh" in n}
    def accessor(number):
        a=j["accessors"][number];v=j["bufferViews"][a["bufferView"]]
        offset=v.get("byteOffset",0)+a.get("byteOffset",0)
        width={"SCALAR":1,"VEC3":3,"VEC4":4}[a["type"]]
        return np.frombuffer(binary,dtype="<f4",count=a["count"]*width,
                             offset=offset).reshape(-1,width).copy()
    channels={}
    for ch in j["animations"][0]["channels"]:
        sampler=j["animations"][0]["samplers"][ch["sampler"]]
        channels[(ch["target"]["node"],ch["target"]["path"])]=(
            accessor(sampler["input"]).flatten(),accessor(sampler["output"]))
    def sample(node,kind,time,original):
        if (node,kind) not in channels:
            return np.asarray(original,dtype=float)
        times,values=channels[(node,kind)]
        t=np.clip(time,times[0],times[-1])
        i=max(0,min(len(times)-2,int(np.searchsorted(times,t,side="right")-1)))
        blend=(t-times[i])/(times[i+1]-times[i])
        value=values[i]*(1-blend)+values[i+1]*blend
        if kind=="rotation":value/=np.linalg.norm(value)
        return value
    camera=np.array([3.25,2.30,7.9])
    forward=np.array([0.,-.02,0.])-camera
    forward/=np.linalg.norm(forward)
    right=np.cross(forward,[0,1,0]);right/=np.linalg.norm(right)
    up=np.cross(right,forward);up/=np.linalg.norm(up)
    light=np.array([.3,.7,1.]);light/=np.linalg.norm(light)
    palette_colors={
        "Pedestal_Base":np.array([20,23,33.]),
        "Dragon_Head":np.array([19,90,227.]),
        "Dragon_Jaw":np.array([19,90,227.]),
        "FalconOS_Typography":np.array([219,239,255.])}
    source={}
    for name,mesh in meshes.items():
        source[name]=(np.asarray(mesh.vertices,dtype=np.float32),
                      np.asarray(mesh.faces,dtype=np.int32),
                      np.asarray(mesh.face_normals,dtype=np.float32))
    W,H=480,360
    palette_sum=np.zeros((192,3),dtype=np.float64)
    palette_count=np.zeros(192,dtype=np.uint64)
    frames=[]
    for frame in range(count):
        time=6.*frame/(count-1)
        img=np.zeros((H,W,3),dtype=np.uint8)
        triangles=[]
        root=sample(0,"translation",time,[0,0,0])
        for name,(original,faces,original_normals) in source.items():
            node=j["nodes"][node_names[name]]
            trans=sample(node_names[name],"translation",time,
                         node.get("translation",[0,0,0]))
            rot=sample(node_names[name],"rotation",time,[0,0,0,1])
            mat=trimesh.transformations.quaternion_matrix(
                [rot[3],*rot[:3]])[:3,:3]
            verts=original@mat.T+trans+root
            normals=original_normals@mat.T
            if name=="Dragon_Jaw":
                parent=j["nodes"][node_names["Dragon_Head"]]
                pt=sample(node_names["Dragon_Head"],"translation",time,
                          parent.get("translation",[0,0,0]))
                pr=sample(node_names["Dragon_Head"],"rotation",time,[0,0,0,1])
                pm=trimesh.transformations.quaternion_matrix(
                    [pr[3],*pr[:3]])[:3,:3]
                verts=(original@mat.T+trans)@pm.T+pt+root
                normals=(original_normals@mat.T)@pm.T
            position=verts-camera
            depth=position@forward
            projected=np.column_stack((W*.5+(position@right)*670/depth,
                                       H*.53-(position@up)*670/depth))
            tri=projected[faces]
            face_depth=depth[faces].mean(axis=1)
            center=verts[faces].mean(axis=1)
            dot=np.einsum("ij,ij->i",normals,camera-center)
            area=(tri[:,1,0]-tri[:,0,0])*(tri[:,2,1]-tri[:,0,1])-(
                tri[:,1,1]-tri[:,0,1])*(tri[:,2,0]-tri[:,0,0])
            visible=(depth[faces]>0.05).all(axis=1)&(dot>0)&(abs(area)>.35)
            ids=np.where(visible)[0]
            lum=np.clip(.4+.55*(normals[ids]@light),.25,1.15)
            col=np.clip(palette_colors[name][None,:]*lum[:,None]+(
                np.array([2,8,25]) if name.startswith("Dragon") else 0),
                0,255).astype(np.uint8)
            for i,c in zip(ids,col):
                triangles.append((face_depth[i],tri[i].astype(np.int32),
                                  tuple(int(x) for x in c[::-1])))
        triangles.sort(key=lambda x:-x[0])
        for z,poly,color in triangles:
            if (poly[:,0]<0).all() or (poly[:,0]>=W).all() or (
                poly[:,1]<0).all() or (poly[:,1]>=H).all():continue
            cv2.fillConvexPoly(img,poly,color,lineType=cv2.LINE_AA)
        rgb=cv2.cvtColor(img,cv2.COLOR_BGR2RGB)
        rgb=cv2.resize(rgb,(360,270),interpolation=cv2.INTER_AREA)
        present=rgb.max(axis=2)>10
        blue=(rgb[:,:,2].astype(np.int32)>rgb[:,:,0].astype(np.int32)*1.5)&present&(rgb[:,:,0]<110)
        dark=present&(~blue)&(rgb[:,:,0]<100)
        bright=present&(~blue)&(~dark)
        values=np.zeros(rgb.shape[:2],dtype=np.uint8)
        values[blue]=32+np.clip((rgb[:,:,2][blue]/8).astype(np.int32),0,63)
        values[dark]=96+np.clip((rgb[:,:,0][dark]/6).astype(np.int32),0,31)
        values[bright]=128+np.clip((rgb[:,:,0][bright]/3.6).astype(np.int32),0,63)
        for i in range(1,192):
            rgb_values=rgb[values==i]
            if len(rgb_values):
                palette_sum[i]+=rgb_values.sum(axis=0)
                palette_count[i]+=len(rgb_values)
        frames.append(values)
    data=bytearray()
    offsets=[0]
    for values in frames:
        raw=values.flatten()
        current=int(raw[0]);length=0
        for v in raw:
            v=int(v)
            if v==current and length<255:length+=1
            else:
                data.extend((length,current))
                current=v;length=1
        data.extend((length,current))
        offsets.append(len(data))
    with open(out,"w",encoding="utf-8") as f:
        f.write("/* Generated from supplied FalconOS GLB geometry and animation. */\n")
        f.write("#define BOOT_MODEL_W 360u\n#define BOOT_MODEL_H 270u\n")
        f.write(f"#define BOOT_MODEL_FRAMES {count}u\n")
        palette=[]
        for i in range(192):
            if palette_count[i]:
                color=np.clip(palette_sum[i]/palette_count[i],0,255).astype(np.uint8)
                palette.append((int(color[0])<<16)|(int(color[1])<<8)|int(color[2]))
            else:palette.append(0)
        f.write("static const u32 boot_model_palette[192]={"+",".join("0x%06xu"%n for n in palette)+"};\n")
        f.write("static const u32 boot_model_offsets[]={"+",".join(str(n)+"u" for n in offsets)+"};\n")
        f.write("static const u8 boot_model_rle[]={\n")
        for i in range(0,len(data),32):
            f.write(",".join(str(n) for n in data[i:i+32])+",\n")
        f.write("};\n")
    print(f"PASS decoded original GLB, {count} animation frames, {len(data)} sprite bytes, output {out}")

if __name__=="__main__":
    ap=argparse.ArgumentParser()
    ap.add_argument("--source",default="build/boot_intro.glb")
    ap.add_argument("--out",default="build/boot_model_frames.inc")
    ap.add_argument("--frames",type=int,default=24)
    args=ap.parse_args()
    files=sorted(pathlib.Path("assets/boot").glob("intro_glb.part*.b64"))
    if len(files)!=16:raise SystemExit(f"need 16 original GLB parts, got {len(files)}")
    raw=base64.b64decode("".join(p.read_text() for p in files),validate=True)
    src=pathlib.Path(args.source);src.parent.mkdir(parents=True,exist_ok=True)
    src.write_bytes(raw)
    bake(src,pathlib.Path(args.out),args.frames)
