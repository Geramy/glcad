#!/usr/bin/env python3
"""Regenerate fixed incoming fixtures using the pinned independent reader environment.

Run deliberately when updating the corpus; CI reads the retained fixtures.
Requires scripts/release-readers-requirements.txt. No GLCAD writer is used.
"""
from pathlib import Path
import json,hashlib,math
import ezdxf
from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox,BRepPrimAPI_MakeCylinder
from OCP.BRepAlgoAPI import BRepAlgoAPI_Cut
from OCP.gp import gp_Ax2,gp_Pnt,gp_Dir
from OCP.STEPControl import STEPControl_Writer,STEPControl_AsIs
from OCP.IFSelect import IFSelect_RetDone
out=Path(__file__).resolve().parent.parent/'glcad/src/ui/tests/fixtures/incoming'
out.mkdir(parents=True,exist_ok=True)
doc=ezdxf.new('R2013'); doc.units=6
for name,weight in [('IncomingOutline',50),('IncomingNotes',25)]:
    doc.layers.new(name,dxfattribs={'lineweight':weight})
m=doc.modelspace()
points=[(0,0),(8,0),(8,5),(0,5)]
for a,b in zip(points,points[1:]+points[:1]):
    m.add_line(a,b,dxfattribs={'layer':'IncomingOutline'})
m.add_circle((4,2.5),1,dxfattribs={'layer':'IncomingOutline'})
m.add_text('Incoming plate',dxfattribs={'insert':(2,5.5),'height':.15,'layer':'IncomingNotes'})
dim=m.add_aligned_dim((0,0),(8,0),distance=.5,override={'dimtxt':.15,'dimasz':.1},dxfattribs={'layer':'IncomingNotes'})
dim.render()
h=m.add_hatch(dxfattribs={'layer':'IncomingOutline'})
h.set_solid_fill()
h.paths.add_polyline_path(points,is_closed=True,flags=1)
h.paths.add_edge_path(flags=0).add_arc((4,2.5),1,0,360,ccw=False)
assert not doc.audit().has_errors
doc.saveas(out/'plate.dxf')
box=BRepPrimAPI_MakeBox(8000,5000,3000).Shape()
cylinder=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(4000,2500,0),gp_Dir(0,0,1)),1000,3000).Shape()
cut=BRepAlgoAPI_Cut(box,cylinder); cut.Build(); assert cut.IsDone()
writer=STEPControl_Writer(); assert writer.Transfer(cut.Shape(),STEPControl_AsIs)==IFSelect_RetDone
assert writer.Write(str(out/'plate.step'))==IFSelect_RetDone
manifest={'source_tools':{'DXF':'ezdxf 1.4.4','STEP':'Open Cascade via cadquery-ocp 8.0.1.0.0'},'dimensions_m':[8,5,3],'through_hole_radius_m':1,'expected_volume_m3':(40-math.pi)*3,'sha256':{}}
for p in (out/'plate.dxf',out/'plate.step'):
    manifest['sha256'][p.name]=hashlib.sha256(p.read_bytes()).hexdigest()
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(out)
