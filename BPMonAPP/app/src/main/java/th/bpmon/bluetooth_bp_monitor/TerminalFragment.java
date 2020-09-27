package th.bpmon.bluetooth_bp_monitor;

import android.app.Activity;
import android.app.AlertDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.content.ComponentName;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.IBinder;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.fragment.app.Fragment;
import androidx.fragment.app.FragmentManager;
import androidx.fragment.app.FragmentTransaction;


import android.text.Spannable;
import android.text.SpannableStringBuilder;
import android.text.method.ScrollingMovementMethod;
import android.text.style.ForegroundColorSpan;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Date;


public class TerminalFragment extends Fragment implements ServiceConnection, SerialListener, View.OnClickListener, OptionsDialog.OptionsDialogListener {



    private enum Connected { False, Pending, True }

    private String deviceAddress;
    private String newline = "\r\n";

    private TextView deviceText;

    private Button readButton;
    private Button timerButton;
    private Button optionsButton;
    private Button getReadingsButton;
    private Button graphButton;
    private Button resetButton;


    private Boolean optionsFlag = false;
    private Boolean readingsFlag = false;
    private Boolean graphFlag = false;

    private String receivedReadings;

    private ArrayList<BPReading> storedReadings = new ArrayList<>();

    private SerialSocket socket;
    private SerialService service;
    private boolean initialStart = true;
    private Connected connected = Connected.False;

    String data;
    private String file = "storedReadings";

    private int timerFrequency;
    private int timerNumReadings;

    /*
     * Lifecycle
     */
    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setHasOptionsMenu(true);
        setRetainInstance(true);
        deviceAddress = getArguments().getString("device");
    }

    @Override
    public void onDestroy() {
        writeReadingsToFile();
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
        writeReadingsToFile();
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

        writeReadingsToFile();
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

    /*
     * UI
     */
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_terminal, container, false);
        deviceText = view.findViewById(R.id.deviceText);                          // TextView performance decreases with number of spans
        deviceText.setTextColor(getResources().getColor(R.color.colorRecieveText)); // set as default color to reduce number of spans
        deviceText.setMovementMethod(new ScrollingMovementMethod());

        readButton = view.findViewById(R.id.readButton);
        timerButton = view.findViewById(R.id.timerButton);
        optionsButton  = view.findViewById(R.id.optionsButton);
        getReadingsButton  = view.findViewById(R.id.getReadingsButton);
        graphButton  = view.findViewById(R.id.graphButton);
        resetButton  = view.findViewById(R.id.resetButton);

        readButton.setOnClickListener(this);
        timerButton.setOnClickListener(this);
        optionsButton.setOnClickListener(this);
        getReadingsButton.setOnClickListener(this);
        graphButton.setOnClickListener(this);
        resetButton.setOnClickListener(this);


        readReadingsFromFile();

        return view;
    }

    @Override
    public void onCreateOptionsMenu(Menu menu, MenuInflater inflater) {
        inflater.inflate(R.menu.menu_terminal, menu);
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        int id = item.getItemId();
        if (id == R.id.clear) {
            deviceText.setText("");
            return true;
        }
        else if (id == R.id.viewReadings){
            deviceText.setText("");
            for(BPReading reading : storedReadings)
            {
                addMessage(reading.toString2());
            }
            return true;
        }
        else if (id == R.id.clearReadings){
            new AlertDialog.Builder(this.getContext())
                    .setTitle("Confirm Clear")
                    .setMessage("Do you really want to clear all readings?")
                    .setIcon(android.R.drawable.ic_dialog_alert)
                    .setPositiveButton(R.string.dialog_clear, new DialogInterface.OnClickListener() {

                        public void onClick(DialogInterface dialog, int whichButton) {
                            switch(whichButton){
                                case -1:
                                    storedReadings.clear();
                                    writeReadingsToFile();
                                    break;
                                default:
                                    break;
                            }
                        }})
                    .setNegativeButton(R.string.dialog_cancel, null).show();
            return true;
        }
        else {
            return super.onOptionsItemSelected(item);
        }
    }


    public void onClick(View v) {
        switch (v.getId()) {
            case  R.id.readButton: {
                send("read");
                break;
            }
            case R.id.timerButton: {
                send("time");
                break;
            }
            case  R.id.optionsButton: {
                send("optr");
                optionsFlag = true;
                break;
            }

            case R.id.getReadingsButton: {
                send("getr");
                readingsFlag = true;
                break;
            }
            case  R.id.graphButton: {
                send("grap");


                if (getFragmentManager().findFragmentByTag("graph")!= null){
                    Fragment fragment = getFragmentManager().findFragmentByTag("graph");
                    getFragmentManager().beginTransaction().replace(R.id.fragment, fragment, "graph").addToBackStack(null).commit();
                    try { getActivity().unbindService(this); } catch(Exception ignored) {}
                }
                else {
                    Bundle args = new Bundle();
                    args.putString("device", deviceAddress);
                    Fragment fragment = new GraphFragment();
                    fragment.setArguments(args);
                    getFragmentManager().beginTransaction().replace(R.id.fragment, fragment, "graph").addToBackStack(null).commit();

                }



                break;
            }

            case R.id.resetButton: {
                send("canc");
                break;
            }


        }
    }


    /*
     * Serial + UI
     */
    private void connect() {
        try {
            BluetoothAdapter bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
            BluetoothDevice device = bluetoothAdapter.getRemoteDevice(deviceAddress);
            String deviceName = device.getName() != null ? device.getName() : device.getAddress();
            status("connecting...");
            connected = Connected.Pending;
            socket = new SerialSocket();
            service.connect(this, "Connected to " + deviceName);
            socket.connect(getContext(), service, device);
        } catch (Exception e) {
            onSerialConnectError(e);
        }
    }

    private void disconnect() {
        connected = Connected.False;
        service.disconnect();
        socket.disconnect();
        socket = null;
    }

    private void send(String str) {
        if(connected != Connected.True) {
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


    /* File Handling Methods */
    private String readFromFile(Context context) {

        String ret = "";

        try {
            InputStream inputStream = context.openFileInput(file);

            if ( inputStream != null ) {
                InputStreamReader inputStreamReader = new InputStreamReader(inputStream);
                BufferedReader bufferedReader = new BufferedReader(inputStreamReader);
                String receiveString = "";
                StringBuilder stringBuilder = new StringBuilder();

                while ( (receiveString = bufferedReader.readLine()) != null ) {
                    stringBuilder.append("\n").append(receiveString);
                }

                inputStream.close();
                ret = stringBuilder.toString();
            }
        }
        catch (FileNotFoundException e) {
            Log.e("login activity", "File not found: " + e.toString());
        } catch (IOException e) {
            Log.e("login activity", "Can not read file: " + e.toString());
        }

        return ret;
    }

    private void writeToFile(String data,Context context) {
        try {
            OutputStreamWriter outputStreamWriter = new OutputStreamWriter(context.openFileOutput(file, Context.MODE_PRIVATE));
            outputStreamWriter.write(data);
            outputStreamWriter.close();
        }
        catch (IOException e) {
            Log.e("Exception", "File write failed: " + e.toString());
        }
    }

    private void writeReadingsToFile(){
        StringBuilder readingsAsString = new StringBuilder();

        for (int i = 0; i < storedReadings.size(); i++){
            readingsAsString.append(storedReadings.get(i).toString());
        }

        writeToFile(readingsAsString.toString(), getContext());
    }

    private void readReadingsFromFile(){
        String readFromFile = readFromFile(getContext());

        if (!readFromFile.isEmpty()) {
            parseAndStoreReceivedReadings(readFromFile);
        }
    }

    private void parseAndStoreReceivedReadings(String readings){
        long dateAsInt = 0;
        float MAP = 0;
        float systolic = 0;
        float diastolic = 0;
        int position = 0;

        readings = readings.replace("\n", "").replace("\r", "");

        String[] tokens = readings.split("/");

        for(int i = 0; i < tokens.length; i++){
            if (!tokens[i].isEmpty() && Character.isDigit(tokens[i].charAt(0))){
                switch (position){
                    case 0:
                        dateAsInt = Long.parseLong(tokens[i]);
                        dateAsInt = dateAsInt * 1000;
                        position += 1;
                        break;
                    case 1:
                        MAP = Float.parseFloat(tokens[i]);
                        position += 1;
                        break;
                    case 2:
                        systolic = Float.parseFloat(tokens[i]);
                        position += 1;
                        break;
                    case 3:
                        diastolic = Float.parseFloat(tokens[i]);
                        storedReadings.add(new BPReading(new Date(dateAsInt), MAP, systolic, diastolic));
                        position = 0;
                        break;
                    default:
                        position = 0;
                }
            }
        }
    }

    /*Message handling methods*/
    private void receive(byte[] data) {

        String recvMsg = new String(data);

        if (optionsFlag){
            if (recvMsg.equals("err1~")) {
                addMessage("Device Date/Time not set, sending Date/Time\n");
                long currentTime = Calendar.getInstance().getTimeInMillis();
                currentTime = currentTime / 1000;
                send(String.valueOf(currentTime));
                optionsFlag = false;
            }
            else{
                String timerOpts = recvMsg.replace("~", "");
                String[] tokens = timerOpts.split("/");


                timerFrequency = Integer.parseInt(tokens[0]);
                timerNumReadings = Integer.parseInt(tokens[1]);
                optionsFlag = false;
                
                openOptionsDialog();
            }
        }
        else if (readingsFlag){
            if (recvMsg.equals("err1~")) {
                addMessage("Device Date/Time not set, sending Date/Time\n");
                long currentTime = Calendar.getInstance().getTimeInMillis();
                currentTime = currentTime / 1000;
                send(String.valueOf(currentTime));
                readingsFlag = false;
            }
            else if (recvMsg.equals("err3~")){
                addMessage("No readings to download from device\n");
                readingsFlag = false;
            }
            else if (recvMsg.equals("endr~")){
                readingsFlag = false;
                Log.d("readings",receivedReadings);
                parseAndStoreReceivedReadings(receivedReadings);
                receivedReadings = "";

            }
            else{
                receivedReadings = receivedReadings + recvMsg;
            }


        }
        else {

            //Device time/date not been set
            if (recvMsg.equals("err1~")) {
                addMessage("Device Date/Time not set, sending Date/Time\n");
                long currentTime = Calendar.getInstance().getTimeInMillis();
                currentTime = currentTime / 1000;
                send(String.valueOf(currentTime));

            } else if (recvMsg.equals("err2~")) {
                addMessage("Device readings full, please get the readings from the device\n");

            } else {
                recvMsg = recvMsg.replace("~", "\n");
                addMessage(recvMsg);
            }

        }
    }

    private void openOptionsDialog() {
        OptionsDialog optionsDialog = new OptionsDialog();

        optionsDialog.setTargetFragment(this, 0);

        Bundle args = new Bundle();
        args.putInt("timerFreq", timerFrequency/60);
        args.putInt("timerNumReadings", timerNumReadings);
        optionsDialog.setArguments(args);

        optionsDialog.show(getFragmentManager(),"Options Dialog");
    }

    private void addMessage(String msg) {
        // append the new string
        deviceText.append(msg);
        final int scrollAmount = deviceText.getLayout().getLineTop(deviceText.getLineCount()) - deviceText.getHeight();
        if (scrollAmount > 0)
            deviceText.scrollTo(0, scrollAmount);
        else
            deviceText.scrollTo(0, 0);
    }

    private void status(String str) {
        SpannableStringBuilder spn = new SpannableStringBuilder(str+'\n');
        spn.setSpan(new ForegroundColorSpan(getResources().getColor(R.color.colorStatusText)), 0, spn.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
        deviceText.append(spn);
    }

    /*
     * SerialListener
     */
    @Override
    public void onSerialConnect() {
        status("connected");
        connected = Connected.True;
    }

    @Override
    public void onSerialConnectError(Exception e) {
        status("connection failed: " + e.getMessage());
        disconnect();
    }

    @Override
    public void onSerialRead(byte[] data) {
        receive(data);
    }

    @Override
    public void onSerialIoError(Exception e) {
        status("connection lost: " + e.getMessage());
        disconnect();
    }

    @Override
    public void applyOptions(int timerFreq, int timerNum) {
        timerFrequency = timerFreq;
        timerNumReadings = timerNum;
        send("opts");
        send("" + timerFrequency * 60);
        send("" +  timerNumReadings);

        deviceText.append("Set timer to every " + timerFrequency + " minutes, with a total of " + timerNumReadings + " readings\n");

    }
}
