#!/bin/bash

#how to use the script
export SAC_USAGE="$0 pathOfGame NewName [--preview]"
export SAC_OPTIONS="--preview : don't apply changes."
export SAC_EXAMPLE="$0 /tmp/heriswap Prototype"
#where the script is
whereAmI=$(cd "$(dirname "$0")" && pwd)
#import cool stuff
source $whereAmI/cool_stuff.sh

######### 0 : Check arguments. #########
   #check arg count
   if [ "$#" -lt 2 ]; then
      error_and_usage_and_quit "Need 2+ args [and NOT '$#']."
   fi

   #check that the gamepath does exist
   gamePath="$1"
   if [ ! -e "$gamePath" ]; then
      error_and_usage_and_quit "The game path '$gamePath' doesn't exist! Abort."
   fi

   # Remove potentials spaces in names and make lower / upper case.
   oldNameUpper=$(cat $gamePath/CMakeLists.txt | grep 'project(' | cut -d '(' -f2 | tr -d ')')
   oldNameLower=$(echo $oldNameUpper | sed 's/^./\l&/')
   newNameLower=$(echo $2 | tr -d " " | sed 's/^./\l&/')
   newNameUpper=$(echo $2 | tr -d " " | sed 's/^./\u&/')

   #check that the new gamepath does NOT exist
   newGamePath=$(cd $gamePath/.. && pwd)"/$newNameLower"
   if [ -e "$newGamePath" ]; then
      error_and_usage_and_quit "$newGamePath already exist! Please delete it to continue! Abort."
   fi

   #look if we have to apply change
   if [ $# = 3 ] && [ $3 = "--preview" ]; then
      applyChanges=""
      info "Changes won't be applied"
   else
      applyChanges="yes"
   fi



   #command to rename file
   renameC="mv"
   info "Do you want to use 'git mv' instead of 'mv' ? (y/N)" $blue
   read confirm
   if [ "$confirm" = "y" ]; then
      renameC="git mv"
   fi
   info "Rename command will be: '$renameC'" $green

   info "
   INFO: the name you chose is '$newNameUpper'
   ********************************************************************************
   * WARNING: don't use a common name else you could overwrite wrong files        *
   * Eg: don't call it 'test' or each files TestPhysics.cpp,... will be renamed!  *
   ********************************************************************************
   " $yellow



######### 2 : Wait for confirmation to continue #########
   info "Confirm ? (y/N)" $blue
   read confirm
   if [[ "$confirm" != "y" && "$confirm" != "Y" ]]; then
      info "Aborted" $red
      exit
   fi

   #now go to the gamePath
   cd $gamePath
   #make it absolute
   gamePath=$(pwd)

######### 3 : Rename files and dir #########
   #add dir to ignore here.
   IGNOREDIR=".git build bin sac gen libs obj"


   IGNOREDIR=$(echo $IGNOREDIR | sed 's| |/ -e /|g')
   IGNOREDIR=" -v -e /$IGNOREDIR/"
   #IGNOREDIR now looks like "-v -e /.git/ -e /build/ -e /bin/ -e /sac/ -e /gen/ -e /libs/"

   #1) directory in reverse order (rename protype/ before prototype/prototype.cpp )
   directories=$(find . -type d -iname "*$oldNameLower*" | grep $IGNOREDIR)

   while [ ! -z "$directories" ]; do
      first=$(echo $directories | cut -d" " -f1)
      new=${first/$oldNameLower/$newNameLower}
      new=${new/$oldNameUpper/$newNameUpper}

      info "Renaming directory $first to $new"
      if [ ! -z $applyChanges ]; then
         $renameC $first $new
      else
         break
      fi

      directories=$(find . -type d -iname "*$oldNameLower*" | grep $IGNOREDIR)
   done


   #2) files
   for file in $(find . -type f -iname "*$oldNameLower*" | grep $IGNOREDIR); do
      new=${file/$oldNameLower/$newNameLower}
      new=${new/$oldNameUpper/$newNameUpper}

      info "Renaming file $file to $new"

      if [ ! -z $applyChanges ]; then
         echo $renameC $file $new
         $renameC $file $new
      fi
   done

   #3) in files
   if [ ! -z $applyChanges ]; then
      for file in $(find . -type f | grep $IGNOREDIR | cut -d/ -f2-); do
         sed -i "s|$oldNameLower|$newNameLower|g" $file
         sed -i "s|$oldNameUpper|$newNameUpper|g" $file
      done
   fi


######### 4 : Clean build dir [optionnal] #########
   info "Want to clean build directory ? (y/N)" $blue
   read confirm
   if [[ "$confirm" == "y" || "$confirm" == "Y" ]]; then
      info "rm -rf build && mkdir build"
      if [ ! -z $applyChanges ]; then
         rm -rf build
      fi
   fi

######### 5 : rename the root dir [optionnal] #########
   info "Want to rename the root dir from '$gamePath' to '$newGamePath' ? (y/N)" $blue
   read confirm
   if [[ "$confirm" == "y" || "$confirm" == "Y" ]]; then
      info "mv $gamePath $newGamePath"
      if [ ! -z $applyChanges ]; then
         mv $gamePath $newGamePath
      fi
   fi

