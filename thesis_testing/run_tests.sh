cd utility

if ! sudo -v; then
        echo "Error: sudo required to run tests."
        exit 1
fi

echo "Running Tests.."
./test.sh

echo "Generating Plots.."
python3 plot_results.py > ../results/latex_tables.txt

cd ..
echo "Done."
