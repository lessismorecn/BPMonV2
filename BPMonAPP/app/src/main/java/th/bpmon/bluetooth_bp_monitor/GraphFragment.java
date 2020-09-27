package th.bpmon.bluetooth_bp_monitor;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.graphics.Color;
import android.os.Bundle;
import android.os.IBinder;
import android.text.Spannable;
import android.text.SpannableStringBuilder;
import android.text.method.ScrollingMovementMethod;
import android.text.style.ForegroundColorSpan;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import com.github.mikephil.charting.charts.LineChart;
import com.github.mikephil.charting.components.YAxis;
import com.github.mikephil.charting.data.Entry;
import com.github.mikephil.charting.data.LineData;
import com.github.mikephil.charting.data.LineDataSet;
import com.github.mikephil.charting.interfaces.datasets.ILineDataSet;

import java.util.ArrayList;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import uk.me.berndporr.iirj.Bessel;
import uk.me.berndporr.iirj.Butterworth;

public class GraphFragment extends Fragment implements ServiceConnection, SerialListener, View.OnClickListener {

    private enum Connected { False, Pending, True }

    private String deviceAddress;
    private String newline = "\r\n";

    private TextView deviceText;

    private Button readButton;
    private Button textButton;
    private Button resetButton;

    private LineChart mChart;
    private Thread thread;
    private boolean plotData = true;

    Butterworth butterworth = new Butterworth();
    Bessel bessel = new Bessel();

    private ArrayList<BPReading> storedReadings = new ArrayList<>();

    private SerialSocket socket;
    private SerialService service;
    private boolean initialStart = true;
    private GraphFragment.Connected connected = GraphFragment.Connected.False;

    String data;
    private String file = "storedReadings";

    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_graph, container, false);
        deviceText = view.findViewById(R.id.deviceText);                          // TextView performance decreases with number of spans
        deviceText.setTextColor(getResources().getColor(R.color.colorRecieveText)); // set as default color to reduce number of spans
        deviceText.setMovementMethod(new ScrollingMovementMethod());

        readButton = view.findViewById(R.id.readButton);
        textButton = view.findViewById(R.id.textButton);
        resetButton  = view.findViewById(R.id.resetButton);

        readButton.setOnClickListener(this);
        textButton.setOnClickListener(this);
        resetButton.setOnClickListener(this);

        mChart = view.findViewById(R.id.chart);
        mChart.getDescription().setEnabled(false);

        mChart.setTouchEnabled(false);
        mChart.setDragEnabled(false);
        mChart.setScaleEnabled(false);
        mChart.setDrawGridBackground(true);
        mChart.setPinchZoom(false);
        mChart.setBackgroundColor(Color.WHITE);

        LineData data = new LineData();
        data.setValueTextColor(Color.WHITE);
        mChart.setData(data);

        butterworth.bandPass(2,50,9.75,19);
        bessel.lowPass(10,50,6);



        //startPlot();

        return view;
    }

    private void startPlot(){
        if(thread != null){
            thread.interrupt();
        }

        thread = new Thread(new Runnable() {
            @Override
            public void run() {
                while (true){
                    plotData = true;
                    try {
                        Thread.sleep(10);
                    }catch (InterruptedException e){
                        e.printStackTrace();
                    }
                }
            }
        });
    }

    private void addEntry(float sensorData, float filterData){
        LineData data = mChart.getData();
        if(data != null){
            ILineDataSet set = data.getDataSetByIndex(0);
            ILineDataSet set2 = data.getDataSetByIndex(1);

            if (set == null) {
                set = createSet();
                data.addDataSet(set);
            }
            if (set2 == null) {
                set2 = createSet2();
                data.addDataSet(set2);
            }

            data.addEntry(new Entry(set.getEntryCount(), sensorData), 0);
            data.addEntry(new Entry(set2.getEntryCount(), filterData), 1);
            data.notifyDataChanged();

            // let the chart know it's data has changed
            mChart.notifyDataSetChanged();

            // limit the number of visible entries
            mChart.setVisibleXRangeMaximum(150);
            // mChart.setVisibleYRange(30, AxisDependency.LEFT);

            // move to the latest entry
            mChart.moveViewToX(data.getEntryCount());
        }

    }

    private LineDataSet createSet(){
        LineDataSet set = new LineDataSet(null, "Sensor Data");
        set.setAxisDependency(YAxis.AxisDependency.LEFT);
        set.setLineWidth(1f);
        set.setColor(Color.BLUE);
        set.setMode(LineDataSet.Mode.CUBIC_BEZIER);
        set.setCubicIntensity(0.05f);
        set.setDrawCircles(false);
        return set;
    }

    private LineDataSet createSet2(){
        LineDataSet set = new LineDataSet(null, "Filter Data");
        set.setAxisDependency(YAxis.AxisDependency.LEFT);
        set.setLineWidth(1f);
        set.setColor(Color.RED);
        set.setMode(LineDataSet.Mode.CUBIC_BEZIER);
        set.setCubicIntensity(0.05f);
        set.setDrawCircles(false);
        return set;
    }


    @Override
    public void onClick(View v) {
        switch (v.getId()) {
            case R.id.readButton: {

                send("read");
                break;
            }
            case R.id.resetButton: {
                send("canc");
                break;
            }
            case R.id.textButton: {
                send("grap");
                Fragment fragment = getFragmentManager().findFragmentByTag("terminal");
                getFragmentManager().beginTransaction().replace(R.id.fragment, fragment, "terminal").addToBackStack(null).commit();
                try { getActivity().unbindService(this); } catch(Exception ignored) {}
                break;
            }
        }

    }

    private void receive(byte[] data) {

        String recvMsg = new String(data);

        String[] tokens = recvMsg.split("~");



        float sensorData;

        for (int i = 0; i < tokens.length; i++) {
            try {
                tokens[i].replace("~", "");
                sensorData = Float.parseFloat(tokens[i]);
                sensorData = (float) bessel.filter(sensorData);
                addEntry(sensorData, (float) butterworth.filter(sensorData));
            } catch (Exception e) {
                addMessage(recvMsg + "\n");
            }
        }
    }

    private void addMessage(String msg) {
        // append the new string
        deviceText.append(msg);
        // find the amount we need to scroll.  This works by
        // asking the TextView's internal layout for the position
        // of the final line and then subtracting the TextView's height
        final int scrollAmount = deviceText.getLayout().getLineTop(deviceText.getLineCount()) - deviceText.getHeight();
        // if there is no need to scroll, scrollAmount will be <=0
        if (scrollAmount > 0)
            deviceText.scrollTo(0, scrollAmount);
        else
            deviceText.scrollTo(0, 0);
    }

    /*
     * SerialListener
     */
    @Override
    public void onSerialConnect() {
        Toast.makeText(getActivity(), "connected", Toast.LENGTH_SHORT).show();
        connected = Connected.True;
    }

    @Override
    public void onSerialConnectError(Exception e) {
        Toast.makeText(getActivity(), "connection failed", Toast.LENGTH_SHORT).show();
        disconnect();
    }

    @Override
    public void onSerialRead(byte[] data) {
        receive(data);
    }

    @Override
    public void onSerialIoError(Exception e) {
        Toast.makeText(getActivity(), "connection lost", Toast.LENGTH_SHORT).show();
        disconnect();
    }

    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setHasOptionsMenu(true);
        setRetainInstance(true);
        deviceAddress = getArguments().getString("device");
    }

    @Override
    public void onDestroy() {
        if (connected != Connected.False)
            disconnect();
        getActivity().stopService(new Intent(getActivity(), SerialService.class));
        super.onDestroy();
    }

    @Override
    public void onStart() {
        super.onStart();

        if(service != null)
            service.attach(this);
        else
            getActivity().startService(new Intent(getActivity(), SerialService.class)); // prevents service destroy on unbind from recreated activity caused by orientation change
    }

    @Override
    public void onStop() {
        if(service != null && !getActivity().isChangingConfigurations())
            service.detach();
        super.onStop();
    }

    @SuppressWarnings("deprecation") // onAttach(context) was added with API 23. onAttach(activity) works for all API versions
    @Override
    public void onAttach(Activity activity) {
        super.onAttach(activity);
        getActivity().bindService(new Intent(getActivity(), SerialService.class), this, Context.BIND_AUTO_CREATE);
    }

    @Override
    public void onDetach() {

        try { getActivity().unbindService(this); } catch(Exception ignored) {}
        super.onDetach();
    }

    @Override
    public void onResume() {
        super.onResume();
        if(initialStart && service !=null) {
            initialStart = false;
            getActivity().runOnUiThread(this::connect);
        }
        //else{
        //    getActivity().bindService(new Intent(getActivity(), SerialService.class), this, Context.BIND_AUTO_CREATE);
        //}
    }

    @Override
    public void onServiceConnected(ComponentName name, IBinder binder) {
        service = ((SerialService.SerialBinder) binder).getService();
        if(initialStart && isResumed()) {
            initialStart = false;
            getActivity().runOnUiThread(this::connect);
        }
    }

    @Override
    public void onServiceDisconnected(ComponentName name) {
        service = null;
    }


    private void connect() {
        try {
            BluetoothAdapter bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
            BluetoothDevice device = bluetoothAdapter.getRemoteDevice(deviceAddress);
            String deviceName = device.getName() != null ? device.getName() : device.getAddress();
            //status("connecting...");
            connected = GraphFragment.Connected.Pending;
            socket = new SerialSocket();
            service.connect(this, "Connected to " + deviceName);
            socket.connect(getContext(), service, device);
        } catch (Exception e) {
            onSerialConnectError(e);
        }
    }

    private void disconnect() {
        connected = GraphFragment.Connected.False;
        service.disconnect();
        socket.disconnect();
        socket = null;
    }

    private void send(String str) {
        if(connected != GraphFragment.Connected.True) {
            Toast.makeText(getActivity(), "not connected", Toast.LENGTH_SHORT).show();
            return;
        }
        try {
            SpannableStringBuilder spn = new SpannableStringBuilder(str + "~");
            spn.setSpan(new ForegroundColorSpan(getResources().getColor(R.color.colorSendText)), 0, spn.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
            byte[] data = (str + "~").getBytes();
            socket.write(data);
        } catch (Exception e) {
            onSerialIoError(e);
        }
    }

}
