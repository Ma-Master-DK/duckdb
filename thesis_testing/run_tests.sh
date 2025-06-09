cd utility

if ! sudo -v; then
        echo "Error: sudo required to run tests."
        exit 1
fi

echo "Running Tests.."
./test.sh

echo "Generating Plots.."
python3 presentation/present_results.py

cd ..
echo "Done."
