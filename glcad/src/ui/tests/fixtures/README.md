# Native compatibility fixtures

`metadata-v1.glcad` was saved by the metadata version 1 writer before
per-space annotation records were introduced. It contains custom text and
dimension styles, named parameters, a material assignment, a suppressed
feature and mate, a dynamic block and an unloaded xref. Keep this fixture
in its original format; the workflow test opens it and saves an upgraded
copy in its temporary directory.
