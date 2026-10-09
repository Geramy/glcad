"""Check native output using the optional, independent development reader."""
import json
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="glcad-dwg-") as folder:
    path = pathlib.Path(folder) / "native.dwg"
    subprocess.run([sys.argv[1], str(path)], check=True)
    result = subprocess.run([sys.argv[2], "-v4", "-O", "JSON", str(path)], capture_output=True, text=True, check=True)
    assert "ERROR" not in result.stderr, result.stderr
    assert "CRC mismatch" not in result.stderr, result.stderr
    data = json.loads(result.stdout)
    entities = data["OBJECTS"]
    lines = [e for e in entities if e.get("entity") == "LINE"]
    assert len([e for e in lines if e.get("entmode")==2])==2500, len(lines)
    assert abs(lines[0]["start"][0] - 1) < 1e-6
    assert abs(lines[0]["end"][1] - 7) < 1e-6
    notes = [e for e in entities if e.get("entity") == "TEXT"]
    assert any(n["text_value"] == "Independent reader" for n in notes), notes
    refs = [e for e in entities if e.get("entity") == "INSERT"]
    assert len(refs) == 3, refs
    marker_header=next(e for e in entities if e.get("object")=="BLOCK_HEADER" and e.get("name")=="Marker")
    marker = next(r for r in refs if r["block_header"][-1]==marker_header["handle"][-1])
    assert marker["scale"] == [2, 3, 1]
    assert abs(marker["rotation"] - 0.5) < 1e-9
    assert data["HEADER"]["INSUNITS"] == 6

    curves=[e for e in entities if e.get("entity")=="SPLINE"]
    assert len(curves)==1 and curves[0]["degree"]==2
    assert len(curves[0]["knots"])==12 and len(curves[0]["ctrl_pts"])==9
    assert abs(curves[0]["ctrl_pts"][1]["w"]-2**-0.5)<1e-12
    dims=[e for e in entities if e.get("entity")=="DIMENSION_LINEAR" and e.get("entmode")==2]
    assert len(dims)==1 and abs(dims[0]["act_measurement"]-4)<1e-6
    assert dims[0]["dimstyle"][-1] and dims[0]["block"][-1]
    hatches=[e for e in entities if e.get("entity")=="HATCH"]
    assert len(hatches)==1 and hatches[0]["is_solid_fill"]==1
    assert abs(hatches[0]["paths"][0]["segs"][0]["radius"]-1)<1e-6
    attrs=[e for e in entities if e.get("entity")=="ATTRIB"]
    assert len(attrs)==1 and attrs[0]["tag"]=="LABEL" and attrs[0]["text_value"]=="Assigned"
    assert attrs[0]["ownerhandle"][-1]==marker["handle"][-1]
    assert any(e.get("object")=="LTYPE" and e.get("name")=="Dashed" for e in entities)

    for kind,measurement in [("DIMENSION_RADIUS",2),("DIMENSION_DIAMETER",4)]:
        radial=[e for e in entities if e.get("entity")==kind]
        assert len(radial)==1 and abs(radial[0]["act_measurement"]-measurement)<1e-6
        a=radial[0]["def_pt"]; b=radial[0]["first_arc_pt"]
        assert abs(sum((x-y)**2 for x,y in zip(a,b))**.5-measurement)<1e-6
    styles=[e for e in entities if e.get("object")=="DICTIONARYWDFLT"]
    assert len(styles)==1 and "Review" in styles[0]["items"]
    review=styles[0]["items"]["Review"][-1]
    assert any(e.get("plotstyle",[0])[-1]==review for e in lines)

    for i, name in enumerate(["Site", "Services"]):
        header=next(e for e in entities if e.get("object")=="BLOCK_HEADER" and e.get("name")==name)
        assert header["blkisxref"] == 1 and header["xrefoverlaid"] == i
        assert header["loaded_bit"] == i
        assert header["xref_pname"] == (["refs/Site.dwg", "../Services.dwg"][i])
        placement=next(r for r in refs if r["block_header"][-1]==header["handle"][-1])
        assert placement["scale"] == [2, 2+i, 1]
        assert abs(placement["ins_pt"][0]-(10+i)) < 1e-6
        assert abs(placement["ins_pt"][1]-(20+i)) < 1e-6
        assert abs(placement["rotation"]-.5) < 1e-12
        assert header["inserts"][0][-1] == placement["handle"][-1]
        by_handle={e["handle"][-1]:e for e in entities}
        extension=by_handle[placement["xdicobjhandle"][-1]]
        assert extension["ownerhandle"][-1]==placement["handle"][-1]
        filters=by_handle[extension["items"]["ACAD_FILTER"][-1]]
        assert filters["ownerhandle"][-1]==extension["handle"][-1]
        clip=by_handle[filters["items"]["SPATIAL"][-1]]
        assert clip["ownerhandle"][-1]==filters["handle"][-1]
        assert clip["object"]=="SPATIAL_FILTER" and clip["display_boundary_on"]==1-i
        expected=([0,0],[4,0],[4,4],[2,2],[0,4]) if i==0 else ([-1,-2],[4,3])
        assert len(clip["clip_verts"])==len(expected)
        assert all(abs(a-b)<1e-6 for p,q in zip(clip["clip_verts"],expected) for a,b in zip(p,q))
        identity=[1,0,0,0,1,0,0,0,1,0,0,0]
        assert clip["inverse_transform"]==identity and clip["transform"]==identity

    leaders=[e for e in entities if e.get("entity")=="LEADER"]
    assert len(leaders)==1 and leaders[0]["arrowhead_on"]==1
    leader=leaders[0]
    assert len(leader["points"])==3 and abs(leader["points"][1][1]-3)<1e-6
    linked={e["handle"][-1]:e for e in entities}
    note=linked[leader["associated_annotation"][-1]]
    assert note["entity"]=="MTEXT" and note["text"]=="Anchor note",note
    assert linked[leader["dimstyle"][-1]]["name"]=="Callouts"

    layouts=sorted((e for e in entities if e.get("object")=="LAYOUT"),key=lambda e:e["tab_order"])
    assert [e["layout_name"] for e in layouts]==["Model","Floor plan","Details"]
    assert data["HEADER"]["TILEMODE"]==0
    assert data["HEADER"]["BLOCK_RECORD_PSPACE"][-1]==layouts[2]["block_header"][-1]
    for i,layout in enumerate(layouts[1:]):
        owner=layout["block_header"][-1]
        assert abs(layout["plotsettings.paper_width"]-([420,297][i]))<1e-9
        sheet_lines=[e for e in lines if e.get("ownerhandle",[0])[-1]==owner]
        assert len(sheet_lines)==1 and abs(sheet_lines[0]["start"][0]-10)<.001
        assert any(n["text_value"]==layout["layout_name"] and n["ownerhandle"][-1]==owner for n in notes)
        viewports=[e for e in entities if e.get("entity")=="VIEWPORT" and e["ownerhandle"][-1]==owner]
        assert len(viewports)==2
        camera=next(e for e in viewports if e["handle"][-1]==layout["active_viewport"][-1])
        vp=next(e for e in viewports if e!=camera)
        assert vp["view_target"]==[3,4,0] and abs(vp["VIEWSIZE"]-7)<1e-9
        assert abs(vp["twist_angle"]+.3)<1e-12 and vp["status_flag"]&0x4000
        assert bool(vp["status_flag"]&0x20000)==bool(i)
        assert len(vp["frozen_layers"])==1
        if i==0:
            clip=by_handle[vp["clip_boundary"][-1]]
            assert clip["entity"]=="LWPOLYLINE" and clip["ownerhandle"][-1]==owner
            assert len(clip["points"])==4

        dim=next(e for e in entities if e.get("entity")=="DIMENSION_LINEAR" and e.get("ownerhandle",[0])[-1]==owner)
        assert abs(dim["act_measurement"]-100)<.001
        style=by_handle[dim["dimstyle"][-1]]
        assert abs(style["DIMTXT"]-2.5)<1e-12 and abs(style["DIMLFAC"]-1)<1e-12
        block=by_handle[owner]
        visited=[]; handle=block["first_entity"][-1]; previous=0
        while handle:
            assert handle not in visited
            entity=by_handle[handle]
            assert entity.get("entmode")==0 and entity["ownerhandle"][-1]==owner
            assert entity["prev_entity"][-1]==previous
            visited.append(handle); previous=handle; handle=entity["next_entity"][-1]
        assert visited[-1]==block["last_entity"][-1]
        assert set(visited)=={e["handle"][-1] for e in entities if e.get("entity") not in (None,"BLOCK","ENDBLK") and e.get("ownerhandle",[0])[-1]==owner}
