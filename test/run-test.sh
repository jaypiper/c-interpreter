mkdir -p build
pass=0
count=0
for i in {0..24}
do
   count=`expr ${count} + 1`
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
      pass=`expr ${pass} + 1`
   fi
done

echo $pass/$count passed!!
