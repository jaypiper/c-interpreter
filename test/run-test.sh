mkdir -p build
for i in {0..24}
do
   idx="$i"
   if [ $i -lt 10 ]; then
      idx="0$i"
   fi
   gcc testlib.c test$idx.c -o build/test$idx
   file_ref=./build/testout$idx-ref.txt
   file_out=./build/testout$idx.txt
   ./build/test$idx &> $file_ref
   ../ast-interpreter/build/ast-interpreter "`cat test$idx.c`" &> $file_out

   if [ "$(diff $file_ref $file_out)" ];
   then
      echo "test$idx fail...."
   else
      echo "test$idx pass!!"
   fi
done
