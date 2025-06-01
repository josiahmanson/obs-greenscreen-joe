mkdir zip_file
mkdir zip_file\obs-plugins
mkdir zip_file\obs-plugins\64bit

copy build_x64\Release\*.dll zip_file\obs-plugins\64bit


mkdir zip_file\data
mkdir zip_file\data\obs-plugins
mkdir zip_file\data\obs-plugins\greenscreen-joe

robocopy /e data zip_file\data\obs-plugins\greenscreen-joe
