#!/bin/sh
app_install_dir="$(dirname $(readlink -f $0))"
current_date=$(date +"%Y-%m-%d")

current_repos_dir="$app_install_dir"

echo "current_repos_dir: $current_repos_dir"

cd $current_repos_dir

appname=`basename $0 | sed s,\.sh$,,`

dirname=`dirname $0`
tmp="${dirname#?}"

if [ "${dirname%$tmp}" != "/" ]; then
dirname=$PWD/$dirname
fi
export LD_LIBRARY_PATH=$PWD/lib:$LD_LIBRARY_PATH
$dirname/$appname "$@"
