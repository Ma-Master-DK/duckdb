cd utility

if ! sudo -v; then
        echo "Error: sudo required to run tests."
        exit 1
fi

echo "Running Tests.."
./test.sh > ../results/test_results.txt

echo "Generating Plots.."
python3 plot_results.py

cd ..
echo "Done."
